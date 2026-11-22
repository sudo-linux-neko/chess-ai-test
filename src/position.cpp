// ChessClaw - position.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "position.h"
#include <sstream>

namespace chessclaw {
namespace {
U64 kPieceKey[12][64]{};
U64 kSideKey = 0, kCastleKey[16]{}, kEpKey[8]{};
bool kZInit = false;

U64 rng_next(U64& s) {
  // splitmix64
  U64 z = (s += U64{0x9E3779B97F4A7C15});
  z = (z ^ (z >> 30)) * U64{0xBF58476D1CE4E5B9};
  z = (z ^ (z >> 27)) * U64{0x94D049BB133111EB};
  return z ^ (z >> 31);
}
}  // namespace

void Position::init_zobrist() {
  if (kZInit) return;
  U64 s = 0xC1A9230923A5F1ULL;
  for (int p = 0; p < 12; ++p)
    for (int sq = 0; sq < 64; ++sq) kPieceKey[p][sq] = rng_next(s);
  kSideKey = rng_next(s);
  for (int i = 0; i < 16; ++i) kCastleKey[i] = rng_next(s);
  for (int i = 0; i < 8; ++i) kEpKey[i] = rng_next(s);
  kZInit = true;
}

U64 Position::compute_hash() const {
  U64 h = 0;
  for (int sq = 0; sq < 64; ++sq)
    if (board_[sq] != Piece::None) h ^= kPieceKey[piece_idx(board_[sq])][sq];
  if (stm_ == Color::Black) h ^= kSideKey;
  h ^= kCastleKey[castling_ & 15];
  if (ep_ >= 0) h ^= kEpKey[file_of(ep_)];
  return h;
}

void Position::recompute_occ_hash_from_scratch() {
  bb_.fill(0);
  for (int sq = 0; sq < 64; ++sq)
    if (board_[sq] != Piece::None) bb_[piece_idx(board_[sq])] |= sq_bb(sq);
  occ_[0] = occ_[1] = 0;
  for (int i = 0; i < 6; ++i) occ_[0] |= bb_[i];
  for (int i = 6; i < 12; ++i) occ_[1] |= bb_[i];
  occ_[2] = occ_[0] | occ_[1];
  hash_ = compute_hash();
}

void Position::set_startpos() {
  set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

bool Position::set_fen(const std::string& fen) {
  init_zobrist();
  init_attacks();
  board_.fill(Piece::None);
  std::istringstream ss(fen);
  std::string board, stm, cast, ep;
  int half = 0, full = 1;
  if (!(ss >> board >> stm >> cast >> ep)) return false;
  ss >> half >> full;
  int r = 7, f = 0;
  for (char c : board) {
    if (c == '/') { r--; f = 0; continue; }
    if (c >= '1' && c <= '8') { f += c - '0'; continue; }
    if (f >= 8 || r < 0) return false;
    Piece p = Piece::None;
    switch (c) {
      case 'P': p = Piece::WP; break; case 'N': p = Piece::WN; break;
      case 'B': p = Piece::WB; break; case 'R': p = Piece::WR; break;
      case 'Q': p = Piece::WQ; break; case 'K': p = Piece::WK; break;
      case 'p': p = Piece::BP; break; case 'n': p = Piece::BN; break;
      case 'b': p = Piece::BB; break; case 'r': p = Piece::BR; break;
      case 'q': p = Piece::BQ; break; case 'k': p = Piece::BK; break;
      default: return false;
    }
    board_[make_sq(f, r)] = p;
    f++;
  }
  stm_ = (stm == "b") ? Color::Black : Color::White;
  castling_ = 0;
  if (cast != "-") {
    for (char c : cast) {
      if (c == 'K') castling_ |= 1;
      else if (c == 'Q') castling_ |= 2;
      else if (c == 'k') castling_ |= 4;
      else if (c == 'q') castling_ |= 8;
    }
  }
  ep_ = -1;
  if (ep != "-") {
    int ef = ep[0] - 'a', er = ep[1] - '1';
    if (on_board(ef, er)) ep_ = make_sq(ef, er);
  }
  halfmove_ = half; fullmove_ = full;
  recompute_occ_hash_from_scratch();
  return true;
}

std::string Position::to_fen() const {
  std::string s;
  for (int r = 7; r >= 0; --r) {
    int empty = 0;
    for (int f = 0; f < 8; ++f) {
      Piece p = board_[make_sq(f, r)];
      if (p == Piece::None) { empty++; continue; }
      if (empty) { s += char('0' + empty); empty = 0; }
      const char* names = "PNBRQKpnbrqk";
      s += names[piece_idx(p)];
    }
    if (empty) s += char('0' + empty);
    if (r) s += '/';
  }
  s += (stm_ == Color::White ? " w " : " b ");
  std::string c;
  if (castling_ & 1) c += 'K';
  if (castling_ & 2) c += 'Q';
  if (castling_ & 4) c += 'k';
  if (castling_ & 8) c += 'q';
  if (c.empty()) c = "-";
  s += c + " ";
  s += (ep_ < 0 ? "-" : sq_name(ep_));
  s += " " + std::to_string(halfmove_) + " " + std::to_string(fullmove_);
  return s;
}

U64 Position::attackers_to(int sq, U64 occ) const {
  U64 a = 0;
  a |= (pawn_attacks_of(Color::Black, sq) & pieces(Color::White, PieceType::Pawn));
  a |= (pawn_attacks_of(Color::White, sq) & pieces(Color::Black, PieceType::Pawn));
  a |= (knight_attacks_of(sq) & (pieces(Color::White, PieceType::Knight) | pieces(Color::Black, PieceType::Knight)));
  a |= (king_attacks_of(sq) & (pieces(Color::White, PieceType::King) | pieces(Color::Black, PieceType::King)));
  U64 rq = pieces(Color::White, PieceType::Rook) | pieces(Color::Black, PieceType::Rook) |
           pieces(Color::White, PieceType::Queen) | pieces(Color::Black, PieceType::Queen);
  U64 bq = pieces(Color::White, PieceType::Bishop) | pieces(Color::Black, PieceType::Bishop) |
           pieces(Color::White, PieceType::Queen) | pieces(Color::Black, PieceType::Queen);
  a |= (rook_attacks_of(sq, occ) & rq);
  a |= (bishop_attacks_of(sq, occ) & bq);
  return a;
}

bool Position::is_attacked(int sq, Color by) const {
  U64 occ = occ_[2];
  // pawns
  U64 pawns = pieces(by, PieceType::Pawn);
  // squares from which a pawn of color `by` attacks sq:
  // invert: pawns that attack sq = pawn attacks of opposite color from sq intersect pawns
  if (pawn_attacks_of(opposite(by), sq) & pawns) return true;
  if (knight_attacks_of(sq) & pieces(by, PieceType::Knight)) return true;
  if (king_attacks_of(sq) & pieces(by, PieceType::King)) return true;
  U64 rq = pieces(by, PieceType::Rook) | pieces(by, PieceType::Queen);
  U64 bq = pieces(by, PieceType::Bishop) | pieces(by, PieceType::Queen);
  if (rook_attacks_of(sq, occ) & rq) return true;
  if (bishop_attacks_of(sq, occ) & bq) return true;
  return false;
}

bool Position::in_check() const {
  U64 k = pieces(stm_, PieceType::King);
  if (!k) return false;
  int ks = lsb_index(k);
  return is_attacked(ks, opposite(stm_));
}

bool Position::make_move(const Move& m, Undo& u) {
  u.move = m; u.captured = Piece::None;
  u.castling = castling_; u.ep = ep_; u.halfmove = halfmove_; u.hash = hash_;
  int from = m.from, to = m.to;
  Piece pc = board_[from];
  if (pc == Piece::None) return false;
  Color us = color_of(pc), them = opposite(us);
  Piece cap = board_[to];
  u.captured = cap;

  // Remove ep hash, will re-add
  // Incremental hash update:
  hash_ ^= kPieceKey[piece_idx(pc)][from];
  if (ep_ >= 0) hash_ ^= kEpKey[file_of(ep_)];
  hash_ ^= kCastleKey[castling_ & 15];

  ep_ = -1;
  halfmove_++;
  if (type_of(pc) == PieceType::Pawn) halfmove_ = 0;

  // Remove piece from from-square bitboards
  bb_[piece_idx(pc)] &= ~sq_bb(from);
  occ_[color_idx(us)] &= ~sq_bb(from);
  occ_[2] &= ~sq_bb(from);
  board_[from] = Piece::None;

  if (m.enpassant) {
    int cap_sq = to + (us == Color::White ? -8 : 8);
    Piece epc = board_[cap_sq];
    u.captured = epc;
    if (epc != Piece::None) {
      hash_ ^= kPieceKey[piece_idx(epc)][cap_sq];
      bb_[piece_idx(epc)] &= ~sq_bb(cap_sq);
      occ_[color_idx(them)] &= ~sq_bb(cap_sq);
      occ_[2] &= ~sq_bb(cap_sq);
      board_[cap_sq] = Piece::None;
    }
    halfmove_ = 0;
  } else if (cap != Piece::None) {
    hash_ ^= kPieceKey[piece_idx(cap)][to];
    bb_[piece_idx(cap)] &= ~sq_bb(to);
    occ_[color_idx(them)] &= ~sq_bb(to);
    occ_[2] &= ~sq_bb(to);
    halfmove_ = 0;
  }

  // Castling rook move
  if (m.castle_k) {
    int rf = us == Color::White ? make_sq(7, 0) : make_sq(7, 7);
    int rt = us == Color::White ? make_sq(5, 0) : make_sq(5, 7);
    Piece rook = board_[rf];
    hash_ ^= kPieceKey[piece_idx(rook)][rf];
    bb_[piece_idx(rook)] &= ~sq_bb(rf);
    board_[rf] = Piece::None;
    occ_[color_idx(us)] &= ~sq_bb(rf); occ_[2] &= ~sq_bb(rf);
    hash_ ^= kPieceKey[piece_idx(rook)][rt];
    bb_[piece_idx(rook)] |= sq_bb(rt);
    board_[rt] = rook;
    occ_[color_idx(us)] |= sq_bb(rt); occ_[2] |= sq_bb(rt);
  } else if (m.castle_q) {
    int rf = us == Color::White ? make_sq(0, 0) : make_sq(0, 7);
    int rt = us == Color::White ? make_sq(3, 0) : make_sq(3, 7);
    Piece rook = board_[rf];
    hash_ ^= kPieceKey[piece_idx(rook)][rf];
    bb_[piece_idx(rook)] &= ~sq_bb(rf);
    board_[rf] = Piece::None;
    occ_[color_idx(us)] &= ~sq_bb(rf); occ_[2] &= ~sq_bb(rf);
    hash_ ^= kPieceKey[piece_idx(rook)][rt];
    bb_[piece_idx(rook)] |= sq_bb(rt);
    board_[rt] = rook;
    occ_[color_idx(us)] |= sq_bb(rt); occ_[2] |= sq_bb(rt);
  }

  // Place moving piece (promotion aware)
  Piece placed = pc;
  if (m.promo) {
    PieceType pt = PieceType::None;
    if (m.promo == 1) pt = PieceType::Knight;
    else if (m.promo == 2) pt = PieceType::Bishop;
    else if (m.promo == 3) pt = PieceType::Rook;
    else pt = PieceType::Queen;
    placed = make_piece(us, pt);
  }
  // Clear destination occupancy bits (already cleared for capture above, but
  // non-capture destination still occupied-bit clean)
  occ_[color_idx(them)] &= ~sq_bb(to);
  occ_[2] &= ~sq_bb(to);
  board_[to] = placed;
  bb_[piece_idx(placed)] |= sq_bb(to);
  occ_[color_idx(us)] |= sq_bb(to);
  occ_[2] |= sq_bb(to);
  hash_ ^= kPieceKey[piece_idx(placed)][to];

  if (m.double_push) {
    ep_ = from + (us == Color::White ? 8 : -8);
    hash_ ^= kEpKey[file_of(ep_)];
  }

  // Update castling rights
  if (type_of(pc) == PieceType::King) {
    castling_ &= ~(us == Color::White ? 3 : 12);
  }
  // Rook moved or captured from corner
  auto clear_sq = [&](int sq, int mask) {
    if (from == sq || to == sq) castling_ &= ~mask;
  };
  clear_sq(make_sq(7, 0), 1); clear_sq(make_sq(0, 0), 2);
  clear_sq(make_sq(7, 7), 4); clear_sq(make_sq(0, 7), 8);

  hash_ ^= kCastleKey[castling_ & 15];
  if (us == Color::Black) fullmove_++;
  stm_ = them;
  hash_ ^= kSideKey;
  return true;
}

void Position::unmake_move(const Move& m, const Undo& u) {
  stm_ = opposite(stm_);
  Color us = stm_;
  int from = m.from, to = m.to;
  Piece placed = board_[to];
  // remove placed
  bb_[piece_idx(placed)] &= ~sq_bb(to);
  occ_[color_idx(us)] &= ~sq_bb(to);
  occ_[2] &= ~sq_bb(to);
  board_[to] = Piece::None;
  // restore rook for castling
  if (m.castle_k) {
    int rf = us == Color::White ? make_sq(7, 0) : make_sq(7, 7);
    int rt = us == Color::White ? make_sq(5, 0) : make_sq(5, 7);
    Piece rook = board_[rt];
    bb_[piece_idx(rook)] &= ~sq_bb(rt);
    board_[rt] = Piece::None;
    occ_[color_idx(us)] &= ~sq_bb(rt); occ_[2] &= ~sq_bb(rt);
    bb_[piece_idx(rook)] |= sq_bb(rf);
    board_[rf] = rook;
    occ_[color_idx(us)] |= sq_bb(rf); occ_[2] |= sq_bb(rf);
  } else if (m.castle_q) {
    int rf = us == Color::White ? make_sq(0, 0) : make_sq(0, 7);
    int rt = us == Color::White ? make_sq(3, 0) : make_sq(3, 7);
    Piece rook = board_[rt];
    bb_[piece_idx(rook)] &= ~sq_bb(rt);
    board_[rt] = Piece::None;
    occ_[color_idx(us)] &= ~sq_bb(rt); occ_[2] &= ~sq_bb(rt);
    bb_[piece_idx(rook)] |= sq_bb(rf);
    board_[rf] = rook;
    occ_[color_idx(us)] |= sq_bb(rf); occ_[2] |= sq_bb(rf);
  }
  // restore mover (unpromote)
  PieceType was = m.promo ? PieceType::Pawn : type_of(placed);
  Piece orig = make_piece(us, was);
  board_[from] = orig;
  bb_[piece_idx(orig)] |= sq_bb(from);
  occ_[color_idx(us)] |= sq_bb(from);
  occ_[2] |= sq_bb(from);
  // restore capture
  if (m.enpassant) {
    int cap_sq = to + (us == Color::White ? -8 : 8);
    board_[cap_sq] = u.captured;
    if (u.captured != Piece::None) {
      bb_[piece_idx(u.captured)] |= sq_bb(cap_sq);
      occ_[color_idx(opposite(us))] |= sq_bb(cap_sq);
      occ_[2] |= sq_bb(cap_sq);
    }
  } else if (u.captured != Piece::None) {
    board_[to] = u.captured;
    bb_[piece_idx(u.captured)] |= sq_bb(to);
    occ_[color_idx(opposite(us))] |= sq_bb(to);
    occ_[2] |= sq_bb(to);
  }
  if (us == Color::Black) fullmove_--;
  castling_ = u.castling; ep_ = u.ep; halfmove_ = u.halfmove; hash_ = u.hash;
}

void Position::make_null(Undo& u) {
  u.move = Move::null(); u.captured = Piece::None;
  u.castling = castling_; u.ep = ep_; u.halfmove = halfmove_; u.hash = hash_;
  if (ep_ >= 0) hash_ ^= kEpKey[file_of(ep_)];
  ep_ = -1;
  stm_ = opposite(stm_);
  hash_ ^= kSideKey;
  halfmove_++;
}

void Position::unmake_null(const Undo& u) {
  stm_ = opposite(stm_);
  ep_ = u.ep; halfmove_ = u.halfmove; hash_ = u.hash;
  castling_ = u.castling;
}

}  // namespace chessclaw
