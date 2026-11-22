// ChessClaw - movegen.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "movegen.h"

namespace chessclaw {
namespace {

void add_pawn_moves(const Position& pos, int from, U64 targets, bool capture_ok, bool quiet_ok,
                    MoveList& pseudo, Color us, int ep_sq) {
  int dir = us == Color::White ? 8 : -8;
  int start_rank = us == Color::White ? 1 : 6;
  int promo_rank = us == Color::White ? 7 : 0;
  while (targets) {
    int to = pop_lsb(targets);
    bool is_cap = pos.piece_on(to) != Piece::None;
    if (is_cap && !capture_ok) continue;
    if (!is_cap && to != ep_sq && !quiet_ok) continue;
    if (rank_of(to) == promo_rank) {
      for (int pr = 1; pr <= 4; ++pr) {
        Move m; m.from = (std::uint16_t)from; m.to = (std::uint16_t)to;
        m.promo = (std::uint8_t)pr; m.capture = is_cap;
        pseudo.push(m);
      }
    } else {
      Move m; m.from = (std::uint16_t)from; m.to = (std::uint16_t)to;
      m.capture = is_cap;
      if (to - from == 2 * dir) m.double_push = true;
      if (to == ep_sq && pos.piece_on(to) == Piece::None) { m.enpassant = true; m.capture = true; }
      pseudo.push(m);
    }
  }
  (void)start_rank;
}

void gen_pseudo(const Position& pos, MoveList& pseudo, bool captures_only) {
  pseudo.clear();
  Color us = pos.side_to_move(), them = opposite(us);
  U64 occ = pos.occupancy();
  U64 own = pos.color_bb(us), foe = pos.color_bb(them);
  int ep_sq = pos.ep_square();

  // Pawns
  U64 pawns = pos.pieces(us, PieceType::Pawn);
  int dir = us == Color::White ? 8 : -8;
  U64 pp = pawns;
  while (pp) {
    int from = pop_lsb(pp);
    int f = file_of(from), r = rank_of(from);
    // captures
    U64 caps = pawn_attacks_of(us, from) & (foe | (ep_sq >= 0 ? sq_bb(ep_sq) : U64{0}));
    add_pawn_moves(pos, from, caps, true, false, pseudo, us, ep_sq);
    if (captures_only) continue;
    int one = from + dir;
    if (one >= 0 && one < 64 && !(occ & sq_bb(one))) {
      U64 q = sq_bb(one);
      add_pawn_moves(pos, from, q, false, true, pseudo, us, ep_sq);
      int start = us == Color::White ? 1 : 6;
      int two = from + 2 * dir;
      if (r == start && !(occ & sq_bb(two))) {
        U64 q2 = sq_bb(two);
        add_pawn_moves(pos, from, q2, false, true, pseudo, us, ep_sq);
      }
    }
    (void)f;
  }
  // Knights
  U64 kn = pos.pieces(us, PieceType::Knight);
  while (kn) {
    int from = pop_lsb(kn);
    U64 t = knight_attacks_of(from) & ~own;
    if (captures_only) t &= foe;
    while (t) {
      int to = pop_lsb(t);
      Move m; m.from = (std::uint16_t)from; m.to = (std::uint16_t)to;
      m.capture = pos.piece_on(to) != Piece::None;
      pseudo.push(m);
    }
  }
  // Sliders
  auto slider = [&](PieceType pt, U64 (*atk)(int, U64)) {
    U64 s = pos.pieces(us, pt);
    while (s) {
      int from = pop_lsb(s);
      U64 t = atk(from, occ) & ~own;
      if (captures_only) t &= foe;
      while (t) {
        int to = pop_lsb(t);
        Move m; m.from = (std::uint16_t)from; m.to = (std::uint16_t)to;
        m.capture = pos.piece_on(to) != Piece::None;
        pseudo.push(m);
      }
    }
  };
  slider(PieceType::Bishop, bishop_attacks_of);
  slider(PieceType::Rook, rook_attacks_of);
  slider(PieceType::Queen, queen_attacks_of);
  // King
  U64 k = pos.pieces(us, PieceType::King);
  if (k) {
    int from = lsb_index(k);
    U64 t = king_attacks_of(from) & ~own;
    if (captures_only) t &= foe;
    while (t) {
      int to = pop_lsb(t);
      Move m; m.from = (std::uint16_t)from; m.to = (std::uint16_t)to;
      m.capture = pos.piece_on(to) != Piece::None;
      pseudo.push(m);
    }
    if (!captures_only) {
      // Castling: squares empty + not attacked
      if (us == Color::White) {
        if ((pos.castling() & 1) && pos.piece_on(make_sq(5,0)) == Piece::None &&
            pos.piece_on(make_sq(6,0)) == Piece::None &&
            !pos.is_attacked(make_sq(4,0), them) && !pos.is_attacked(make_sq(5,0), them) &&
            !pos.is_attacked(make_sq(6,0), them)) {
          Move m; m.from = make_sq(4,0); m.to = make_sq(6,0); m.castle_k = true;
          pseudo.push(m);
        }
        if ((pos.castling() & 2) && pos.piece_on(make_sq(3,0)) == Piece::None &&
            pos.piece_on(make_sq(2,0)) == Piece::None && pos.piece_on(make_sq(1,0)) == Piece::None &&
            !pos.is_attacked(make_sq(4,0), them) && !pos.is_attacked(make_sq(3,0), them) &&
            !pos.is_attacked(make_sq(2,0), them)) {
          Move m; m.from = make_sq(4,0); m.to = make_sq(2,0); m.castle_q = true;
          pseudo.push(m);
        }
      } else {
        if ((pos.castling() & 4) && pos.piece_on(make_sq(5,7)) == Piece::None &&
            pos.piece_on(make_sq(6,7)) == Piece::None &&
            !pos.is_attacked(make_sq(4,7), them) && !pos.is_attacked(make_sq(5,7), them) &&
            !pos.is_attacked(make_sq(6,7), them)) {
          Move m; m.from = make_sq(4,7); m.to = make_sq(6,7); m.castle_k = true;
          pseudo.push(m);
        }
        if ((pos.castling() & 8) && pos.piece_on(make_sq(3,7)) == Piece::None &&
            pos.piece_on(make_sq(2,7)) == Piece::None && pos.piece_on(make_sq(1,7)) == Piece::None &&
            !pos.is_attacked(make_sq(4,7), them) && !pos.is_attacked(make_sq(3,7), them) &&
            !pos.is_attacked(make_sq(2,7), them)) {
          Move m; m.from = make_sq(4,7); m.to = make_sq(2,7); m.castle_q = true;
          pseudo.push(m);
        }
      }
    }
  }
}

}  // namespace

void generate_legal(const Position& pos, MoveList& out) {
  MoveList pseudo;
  gen_pseudo(pos, pseudo, false);
  out.clear();
  Position tmp = pos;
  for (int i = 0; i < pseudo.size; ++i) {
    Undo u;
    if (!tmp.make_move(pseudo.moves[i], u)) continue;
    // After make, side flipped; king of mover must not be attacked.
    Color mover = opposite(tmp.side_to_move());
    U64 k = tmp.pieces(mover, PieceType::King);
    bool ok = true;
    if (k) ok = !tmp.is_attacked(lsb_index(k), tmp.side_to_move());
    tmp.unmake_move(pseudo.moves[i], u);
    if (ok) out.push(pseudo.moves[i]);
  }
}

void generate_captures(const Position& pos, MoveList& out) {
  MoveList pseudo;
  gen_pseudo(pos, pseudo, true);
  // Add quiet promotions are captures_only-excluded above for pawns? Our pawn path
  // only emits captures in captures_only mode, but quiet promotions matter in qsearch.
  // Emit them explicitly:
  {
    Color us = pos.side_to_move();
    U64 pawns = pos.pieces(us, PieceType::Pawn);
    int dir = us == Color::White ? 8 : -8;
    int pre = us == Color::White ? 6 : 1;
    U64 occ = pos.occupancy();
    U64 c = pawns;
    while (c) {
      int from = pop_lsb(c);
      if (rank_of(from) != pre) continue;
      int to = from + dir;
      if (to >= 0 && to < 64 && !(occ & sq_bb(to))) {
        for (int pr = 1; pr <= 4; ++pr) {
          Move m; m.from=(std::uint16_t)from; m.to=(std::uint16_t)to; m.promo=(std::uint8_t)pr;
          bool dup=false;
          for (int i=0;i<pseudo.size;++i) if(pseudo.moves[i]==m){dup=true;break;}
          if(!dup) pseudo.push(m);
        }
      }
    }
  }
  out.clear();
  Position tmp = pos;
  // If in check, evasions = all legal (qsearch must handle check).
  if (pos.in_check()) { generate_legal(pos, out); return; }
  for (int i = 0; i < pseudo.size; ++i) {
    Undo u;
    if (!tmp.make_move(pseudo.moves[i], u)) continue;
    Color mover = opposite(tmp.side_to_move());
    U64 k = tmp.pieces(mover, PieceType::King);
    bool ok = true;
    if (k) ok = !tmp.is_attacked(lsb_index(k), tmp.side_to_move());
    tmp.unmake_move(pseudo.moves[i], u);
    if (ok) out.push(pseudo.moves[i]);
  }
}

bool has_legal(const Position& pos) {
  MoveList ml;
  generate_legal(pos, ml);
  return ml.size > 0;
}

U64 perft(Position& pos, int depth) {
  if (depth == 0) return 1;
  MoveList ml;
  generate_legal(pos, ml);
  if (depth == 1) return (U64)ml.size;
  U64 n = 0;
  for (int i = 0; i < ml.size; ++i) {
    Undo u;
    pos.make_move(ml.moves[i], u);
    n += perft(pos, depth - 1);
    pos.unmake_move(ml.moves[i], u);
  }
  return n;
}

}  // namespace chessclaw
