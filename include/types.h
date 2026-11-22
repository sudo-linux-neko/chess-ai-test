// ChessClaw - types.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include <cstdint>
#include <string>

namespace chessclaw {

using U64 = std::uint64_t;
using I32 = std::int32_t;
using Score = int;

enum class Color : int { White = 0, Black = 1 };
enum class PieceType : int { Pawn = 0, Knight, Bishop, Rook, Queen, King, None };
enum class Piece : int {
  WP = 0, WN, WB, WR, WQ, WK,
  BP, BN, BB, BR, BQ, BK, None
};

constexpr int NSQ = 64;
constexpr Score INF = 30000;
constexpr Score MATE = 29000;
constexpr Score MATE_IN_MAX = 1000;

inline constexpr Color opposite(Color c) { return c == Color::White ? Color::Black : Color::White; }
inline constexpr int color_idx(Color c) { return static_cast<int>(c); }
inline constexpr int piece_idx(Piece p) { return static_cast<int>(p); }
inline constexpr PieceType type_of(Piece p) {
  if (p == Piece::None) return PieceType::None;
  return static_cast<PieceType>(piece_idx(p) % 6);
}
inline constexpr Color color_of(Piece p) {
  return piece_idx(p) < 6 ? Color::White : Color::Black;
}
inline constexpr Piece make_piece(Color c, PieceType t) {
  return static_cast<Piece>(color_idx(c) * 6 + static_cast<int>(t));
}

inline constexpr int file_of(int sq) { return sq & 7; }
inline constexpr int rank_of(int sq) { return sq >> 3; }
inline constexpr int make_sq(int f, int r) { return r * 8 + f; }
inline constexpr bool on_board(int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; }
inline constexpr U64 sq_bb(int sq) { return U64{1} << sq; }

// Move encoding: 16-bit compact.
// bits 0-5: from, 6-11: to, 12-14: promo piece (0 none,1 N,2 B,3 R,4 Q), 15: unused
// Special flags carried separately: is_capture, is_double_push, is_ep, is_castle
// stored in Move struct for clarity and cheap copy.
struct Move {
  std::uint16_t from = 0;
  std::uint16_t to = 0;
  std::uint8_t promo = 0;  // 0 none, 1 N, 2 B, 3 R, 4 Q
  bool capture = false;
  bool double_push = false;
  bool enpassant = false;
  bool castle_k = false;
  bool castle_q = false;

  constexpr bool is_null() const { return from == 0 && to == 0 && promo == 0 && !capture; }
  constexpr bool is_promotion() const { return promo != 0; }
  constexpr std::uint16_t packed() const {
    return static_cast<std::uint16_t>(from | (to << 6) | (promo << 12));
  }
  static constexpr Move null() { return Move{}; }
  static Move from_uci(const std::string& s);
  std::string to_uci() const;
  bool operator==(const Move& o) const {
    return from == o.from && to == o.to && promo == o.promo;
  }
};

inline std::string sq_name(int sq) {
  std::string s;
  s += char('a' + file_of(sq));
  s += char('1' + rank_of(sq));
  return s;
}

}  // namespace chessclaw
