// ChessClaw - position.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include <array>
#include <string>
#include <vector>
#include "bitboard.h"
#include "types.h"

namespace chessclaw {

struct Undo {
  Move move{};
  Piece captured = Piece::None;
  int castling = 0;
  int ep = -1;
  int halfmove = 0;
  U64 hash = 0;
};

class Position {
 public:
  Position() { set_startpos(); }

  void set_startpos();
  bool set_fen(const std::string& fen);
  std::string to_fen() const;

  Color side_to_move() const { return stm_; }
  U64 hash() const { return hash_; }
  int castling() const { return castling_; }
  int ep_square() const { return ep_; }
  U64 occupancy() const { return occ_[2]; }
  U64 pieces(Color c, PieceType t) const { return bb_[color_idx(c) * 6 + (int)t]; }
  U64 color_bb(Color c) const { return occ_[color_idx(c)]; }
  Piece piece_on(int sq) const { return board_[sq]; }
  bool in_check() const;
  bool is_attacked(int sq, Color by) const;
  U64 attackers_to(int sq, U64 occ) const;

  bool make_move(const Move& m, Undo& u);
  void unmake_move(const Move& m, const Undo& u);

  // Null move for pruning (only when not in check).
  void make_null(Undo& u);
  void unmake_null(const Undo& u);

  static void init_zobrist();

 private:
  std::array<Piece, 64> board_{};
  // bb_[color*6+type]
  std::array<U64, 12> bb_{};
  std::array<U64, 3> occ_{};  // white, black, both
  Color stm_ = Color::White;
  int castling_ = 0;  // 1 K, 2 Q, 4 k, 8 q
  int ep_ = -1;
  int halfmove_ = 0, fullmove_ = 1;
  U64 hash_ = 0;

  void recompute_occ_hash_from_scratch();
  U64 compute_hash() const;
  friend struct Movegen;
};

}  // namespace chessclaw
