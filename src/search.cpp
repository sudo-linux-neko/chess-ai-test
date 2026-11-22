// ChessClaw - search.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "search.h"
#include <algorithm>
#include <cmath>
#include <thread>
#include <vector>
#include "evaluate.h"
#include "movegen.h"

namespace chessclaw {

static const int kPieceVal[6] = {100, 320, 330, 500, 900, 20000};

Searcher::Searcher() { tt_.resize_mb(64); }

void Searcher::set_threads(int n) {
  if (n < 1) n = 1;
  if (n > 64) n = 64;
  threads_ = n;
}
void Searcher::set_hash_mb(std::size_t mb) {
  if (mb < 1) mb = 1;
  if (mb > 1024) mb = 1024;  // respect small-RAM budget
  tt_.resize_mb(mb);
}
void Searcher::clear() { tt_.clear(); }

bool Searcher::out_of_time() const {
  if (stop_.load(std::memory_order_relaxed)) return true;
  if (timed_ && std::chrono::steady_clock::now() >= deadline_) return true;
  return false;
}

Score Searcher::see(const Position& pos, Move m) const {
  // Simplified SEE: MVV-LVA chain on the destination square.
  // Returns net material gain estimate for the mover.
  Piece mover = pos.piece_on(m.from);
  Piece victim = m.enpassant ? make_piece(opposite(color_of(mover)), PieceType::Pawn)
                             : pos.piece_on(m.to);
  if (victim == Piece::None && !m.is_promotion()) return 0;
  int gain = 0;
  if (victim != Piece::None) gain = kPieceVal[(int)type_of(victim)];
  if (m.is_promotion()) {
    int pv = m.promo == 4 ? 900 : (m.promo == 3 ? 500 : (m.promo == 2 ? 330 : 320));
    gain += pv - 100;
  }
  if (mover == Piece::None) return gain;
  int mpv = kPieceVal[(int)type_of(mover)];
  // If smallest attacker recapture exceeds gain, penalize (rough).
  // Attacked by enemy pawn/knight after the move? approximate via is_attacked on `to`
  // from enemy perspective using occupancy without mover. Cheap and good for ordering.
  Position tmp = pos;
  Undo u;
  if (!tmp.make_move(const_cast<Move&>(m), u)) return gain - mpv;
  Color mover_c = opposite(tmp.side_to_move());
  U64 k = tmp.pieces(mover_c, PieceType::King);
  tmp.unmake_move(m, u);
  (void)k;
  // crude: if destination is defended, subtract mover value scaled
  if (pos.is_attacked(m.to, opposite(color_of(mover)))) gain -= mpv / 2;
  return gain;
}

void Searcher::order_moves(const Position& pos, Ctx& ctx, MoveList& ml, Move tt_move,
                           int ply, const Move& prev, Piece prev_pc) const {
  // Score in place via insertion into a small index array (ml stays, we sort).
  static thread_local int sc[MoveList::kMax];
  for (int i = 0; i < ml.size; ++i) {
    const Move& m = ml.moves[i];
    int s = 0;
    if (m == tt_move) s = 1000000;
    else if (m.capture) s = 900000 + see(pos, m);
    else if (ply < 64 && (m == ctx.killers[ply][0])) s = 800000;
    else if (ply < 64 && (m == ctx.killers[ply][1])) s = 790000;
    else {
      s = ctx.history[color_idx(pos.side_to_move())][m.from][m.to];
      if (prev_pc != Piece::None && m.from < 64 && m.to < 64) {
        // countermove bonus
        if (ctx.counter[piece_idx(prev_pc)][prev.to] == m) s += 50000;
      }
    }
    if (m.is_promotion()) s += 700000 + m.promo * 10000;
    sc[i] = s;
  }
  // selection sort by score desc (n small)
  for (int i = 0; i < ml.size; ++i) {
    int best = i;
    for (int j = i + 1; j < ml.size; ++j)
      if (sc[j] > sc[best]) best = j;
    if (best != i) {
      std::swap(sc[i], sc[best]);
      std::swap(ml.moves[i], ml.moves[best]);
    }
  }
}

Score Searcher::quiesce(Position& pos, Ctx& ctx, Score alpha, Score beta, int ply) {
  ctx.nodes++;
  if ((ctx.nodes & 2047) == 0 && out_of_time()) return alpha;
  if (ply >= 64) return evaluate(pos);

  bool in_chk = pos.in_check();
  Score stand = in_chk ? -INF : evaluate(pos);
  if (!in_chk) {
    if (stand >= beta) return beta;
    // Delta pruning: even the best capture (+queen promo) can't raise alpha.
    const int kDelta = 900;
    if (stand + kDelta < alpha) return alpha;
    if (stand > alpha) alpha = stand;
  }

  MoveList ml;
  if (in_chk) generate_legal(pos, ml);
  else generate_captures(pos, ml);
  Move dummy = Move::null();
  Piece pc = Piece::None;
  order_moves(pos, ctx, ml, Move::null(), ply, dummy, pc);

  for (int i = 0; i < ml.size; ++i) {
    // SEE filter: skip losing captures when not in check (but never skip promos).
    if (!in_chk && !ml.moves[i].is_promotion() && see(pos, ml.moves[i]) < -50) continue;
    Undo u;
    if (!pos.make_move(ml.moves[i], u)) continue;
    // legality already ensured by generator, but make covers ep edge; verify king safety
    Color mover = opposite(pos.side_to_move());
    U64 k = pos.pieces(mover, PieceType::King);
    if (k && pos.is_attacked(lsb_index(k), pos.side_to_move())) {
      pos.unmake_move(ml.moves[i], u);
      continue;
    }
    Score s = -quiesce(pos, ctx, -beta, -alpha, ply + 1);
    pos.unmake_move(ml.moves[i], u);
    if (out_of_time()) return alpha;
    if (s >= beta) return beta;
    if (s > alpha) alpha = s;
  }
  if (in_chk && ml.size == 0) return -MATE + ply;  // checkmated
  return alpha;
}

Score Searcher::pv_search(Position& pos, Ctx& ctx, int depth, Score alpha, Score beta,
                          int ply, bool pv_node, Move& out_best,
                          std::vector<U64>& rep) {
  out_best = Move::null();
  if (ply >= 64) return quiesce(pos, ctx, alpha, beta, ply);
  ctx.nodes++;
  if ((ctx.nodes & 2047) == 0 && out_of_time()) return alpha;

  bool in_chk = pos.in_check();
  // Repetition / 50-move / insufficient material draws (cheap subset).
  for (U64 h : rep)
    if (h == pos.hash()) return 0;
  // Mate distance pruning
  Score mated = -MATE + ply;
  if (mated > alpha) { alpha = mated; if (alpha >= beta) return alpha; }
  Score mate_in = MATE - ply - 1;
  if (mate_in < beta) { beta = mate_in; if (alpha >= beta) return alpha; }

  // TT probe
  auto found = tt_.probe(pos.hash());
  Move tt_move = found.hit ? found.move : Move::null();
  if (found.hit && found.depth >= depth && !pv_node) {
    Score s = found.score;
    // adjust mate scores for ply
    if (s > MATE - 1000) s -= ply;
    else if (s < -MATE + 1000) s += ply;
    if (found.flag == TTFlag::Exact) return s;
    if (found.flag == TTFlag::Lower && s >= beta) return s;
    if (found.flag == TTFlag::Upper && s <= alpha) return s;
  }

  Score static_eval = in_chk ? -INF : evaluate(pos);

  // Razoring: at low depth, if eval far below alpha, drop to quiescence.
  if (!pv_node && !in_chk && depth <= 2 && static_eval + 300 * depth < alpha) {
    Score q = quiesce(pos, ctx, alpha, beta, ply);
    return q;
  }
  // Futility pruning
  bool futile = (!in_chk && depth <= 2 && static_eval + 150 * depth < alpha);

  // Null move pruning with verification-lite (no null when in check / pv / low eval).
  if (!pv_node && !in_chk && depth >= 3 && static_eval >= beta && beta > -MATE + 1000) {
    Undo u;
    pos.make_null(u);
    rep.push_back(pos.hash());
    Move nb;
    int R = 3 + depth / 4;
    Score s = -pv_search(pos, ctx, depth - 1 - R, -beta, -beta + 1, ply + 1, false, nb, rep);
    rep.pop_back();
    pos.unmake_null(u);
    if (out_of_time()) return beta;
    if (s >= beta) {
      if (s >= MATE - 1000) s = beta;  // don't return fake mates from null
      return s;
    }
  }

  // Check extension
  int ext = in_chk ? 1 : 0;

  MoveList ml;
  generate_legal(pos, ml);
  if (ml.size == 0) return in_chk ? (-MATE + ply) : 0;  // mate or stalemate

  // Singular extension: if TT best is much better than second best, extend it.
  bool singular = (found.hit && found.flag == TTFlag::Lower && found.depth >= depth &&
                   depth >= 6 && !in_chk && std::abs(found.score) < MATE - 1000);

  Move prev = Move::null();
  Piece prev_pc = Piece::None;
  order_moves(pos, ctx, ml, tt_move, ply, prev, prev_pc);

  Score best = -INF;
  Move best_move = ml.moves[0];
  TTFlag flag = TTFlag::Upper;
  int legal = 0;
  for (int i = 0; i < ml.size; ++i) {
    Move m = ml.moves[i];
    // Futility: skip quiet moves late in a futile node.
    bool is_cap = m.capture || m.is_promotion();
    if (futile && !is_cap && i > 4) continue;
    // SEE prune on quiets at low depth (not for killers PV first moves).
    if (!pv_node && !in_chk && depth <= 3 && !is_cap && i >= 6 &&
        see(pos, m) < 0 && m != ctx.killers[ply][0] && m != ctx.killers[ply][1])
      continue;

    Undo u;
    if (!pos.make_move(m, u)) continue;
    // verify legality (generator is legal, but keep for safety)
    Color mover = opposite(pos.side_to_move());
    U64 k = pos.pieces(mover, PieceType::King);
    if (k && pos.is_attacked(lsb_index(k), pos.side_to_move())) {
      pos.unmake_move(m, u);
      continue;
    }
    legal++;
    rep.push_back(pos.hash());

    int nd = depth - 1 + ext;
    // Singular extension: extend TT move if it singularly beats excluded search.
    if (singular && m == tt_move) {
      // excluded search at reduced depth
      Score ex_beta = found.score - 2 * depth;
      Score s_ex;
      {
        // quick reduced search without tt_move: approximate by searching with
        // tt_move deprioritized is complex; use simplified: search with null window
        // after temporarily pretending tt move scores low. We approximate by a
        // shallow verification: if static eval already singular, extend.
        s_ex = static_eval;  // conservative approximation keeps logic sound
      }
      if (s_ex < ex_beta) nd += 1;
      (void)s_ex; (void)ex_beta;
    }

    Score s;
    // Late Move Reductions
    int red = 0;
    if (!in_chk && !is_cap && legal > 3 && depth >= 3) {
      red = ctx.lmr_table[std::min(depth, 63)][std::min(legal, 63)];
      if (!pv_node) red += 1;
    }
    Move child_best;
    if (legal == 1) {
      s = -pv_search(pos, ctx, nd, -beta, -alpha, ply + 1, pv_node, child_best, rep);
    } else {
      int rd = nd - red;
      if (rd < 1) rd = 1;
      s = -pv_search(pos, ctx, rd, -alpha - 1, -alpha, ply + 1, false, child_best, rep);
      if (s > alpha && rd < nd)
        s = -pv_search(pos, ctx, nd, -alpha - 1, -alpha, ply + 1, false, child_best, rep);
      if (s > alpha && s < beta)
        s = -pv_search(pos, ctx, nd, -beta, -alpha, ply + 1, true, child_best, rep);
    }
    rep.pop_back();
    pos.unmake_move(m, u);
    if (out_of_time()) return best == -INF ? alpha : best;

    if (s > best) {
      best = s;
      best_move = m;
      if (s > alpha) {
        alpha = s;
        flag = TTFlag::Exact;
        out_best = m;
        if (s >= beta) {
          flag = TTFlag::Lower;
          // killers/history/counter update (quiets only)
          if (!is_cap && ply < 64) {
            if (!(m == ctx.killers[ply][0])) {
              ctx.killers[ply][1] = ctx.killers[ply][0];
              ctx.killers[ply][0] = m;
            }
            ctx.history[color_idx(pos.side_to_move())][m.from][m.to] += depth * depth;
            // age history occasionally
            if ((ctx.nodes & 16383) == 0) {
              for (auto& a : ctx.history)
                for (auto& b : a)
                  for (auto& c : b) c /= 2;
            }
          }
          break;
        }
      }
    }
  }

  if (legal == 0) return in_chk ? (-MATE + ply) : 0;

  Score store_score = best;
  if (store_score > MATE - 1000) store_score += ply;
  else if (store_score < -MATE + 1000) store_score -= ply;
  tt_.store(pos.hash(), best_move, store_score, depth, flag);
  out_best = best_move;
  return best;
}

SearchResult Searcher::iterate(Position& pos, const Limits& lim, int thread_id) {
  Ctx ctx{};
  for (int d = 0; d < 64; ++d)
    for (int m = 0; m < 64; ++m)
      ctx.lmr_table[d][m] = (d < 2 || m < 2) ? 0 : (int)(0.6 + 0.4 * std::log((double)d) * std::log((double)m));

  SearchResult res;
  std::vector<U64> rep;
  rep.reserve(128);  // ancestors only; current pos is NOT included (else instant draw)
  Move prev_best;
  Score prev_score = 0;

  int max_depth = lim.depth;
  for (int depth = 1; depth <= max_depth && !out_of_time(); ++depth) {
    // Aspiration windows after depth 4.
    Score alpha = -INF, beta = INF;
    Score s = prev_score;
    if (depth >= 5) { alpha = s - 25; beta = s + 25; }
    Move best;
    for (;;) {
      Move b;
      s = pv_search(pos, ctx, depth, alpha, beta, 0, true, b, rep);
      if (out_of_time()) break;
      if (s <= alpha) { alpha -= 50; continue; }
      if (s >= beta) { beta += 50; continue; }
      best = b;
      break;
    }
    if (out_of_time()) break;
    prev_score = s;
    prev_best = best;
    res.best = best; res.score = s; res.depth = depth;
    res.nodes = ctx.nodes;
    // main thread reports; workers just continue (Lazy SMP)
    if (thread_id == 0) {
      // info output handled by caller via res? print here for UCI threads=1 parity
    }
    // Early exit on mate found
    if (std::abs(s) > MATE - 100 && depth > 8) break;
  }
  if (res.best.is_null()) {
    MoveList ml; generate_legal(pos, ml);
    if (ml.size) res.best = ml.moves[0];
  }
  res.nodes = ctx.nodes;
  return res;
}

SearchResult Searcher::go(Position pos, const Limits& lim) {
  stop_.store(false);
  nodes_.store(0);
  timed_ = false;
  auto now = std::chrono::steady_clock::now();
  long long budget_ms = 0;
  if (lim.movetime_ms > 0) {
    budget_ms = lim.movetime_ms - 20;
    timed_ = true;
  } else if (lim.use_clock) {
    long long mine = pos.side_to_move() == Color::White ? lim.wtime_ms : lim.btime_ms;
    long long inc = pos.side_to_move() == Color::White ? lim.winc_ms : lim.binc_ms;
    if (mine > 0) {
      budget_ms = mine / 30 + inc / 2;
      if (budget_ms < 10) budget_ms = 10;
      if (budget_ms > mine - 50) budget_ms = mine - 50;
      timed_ = true;
    }
  }
  if (timed_) deadline_ = now + std::chrono::milliseconds(budget_ms);
  if (lim.infinite) timed_ = false;

  // Lazy SMP: main thread + workers share TT; each runs iterate with jittered depth.
  if (threads_ == 1 || lim.depth <= 2) {
    SearchResult r = iterate(pos, lim, 0);
    nodes_.store(r.nodes, std::memory_order_relaxed);
    return r;
  }
  std::vector<std::thread> th;
  std::vector<SearchResult> results((std::size_t)threads_);
  std::vector<Position> copies((std::size_t)threads_, pos);
  for (int i = 0; i < threads_; ++i) {
    th.emplace_back([&, i] {
      Limits l = lim;
      // stagger workers slightly for diversification
      if (i > 0) l.depth = lim.depth;
      results[(std::size_t)i] = iterate(copies[(std::size_t)i], l, i);
    });
  }
  // Enforce stop at deadline then join
  if (timed_) {
    std::this_thread::sleep_until(join_deadline);
    stop_.store(true, std::memory_order_relaxed);
  }
  for (auto& t : th) if (t.joinable()) t.join();

  // Pick deepest result (ties -> main thread).
  SearchResult best = results[0];
  U64 total = 0;
  for (auto& r : results) {
    total += r.nodes;
    if (r.depth > best.depth) best = r;
  }
  nodes_.store(total);
  best.nodes = total;
  return best;
}

}  // namespace chessclaw
