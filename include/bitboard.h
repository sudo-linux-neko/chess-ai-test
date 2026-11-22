// ChessClaw - bitboard.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include "types.h"

namespace chessclaw {

// Portable bit ops (no UB, no platform assumptions).
inline int popcount64(U64 b) {
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_popcountll(b);
#else
  int c = 0;
  while (b) { b &= b - 1; ++c; }
  return c;
#endif
}
inline int lsb_index(U64 b) {  // b must be nonzero
#if defined(__GNUC__) || defined(__clang__)
  return __builtin_ctzll(b);
#else
  int i = 0;
  while (!(b & 1u)) { b >>= 1; ++i; }
  return i;
#endif
}
inline int pop_lsb(U64& b) {
  int i = lsb_index(b);
  b &= b - 1;
  return i;
}

void init_attacks();
U64 knight_attacks_of(int sq);
U64 king_attacks_of(int sq);
U64 pawn_attacks_of(Color c, int sq);  // squares attacked FROM sq by pawn of color c
U64 rook_attacks_of(int sq, U64 occ);  // ray-based, no magics (portable fallback)
U64 bishop_attacks_of(int sq, U64 occ);
inline U64 queen_attacks_of(int sq, U64 occ) {
  return rook_attacks_of(sq, occ) | bishop_attacks_of(sq, occ);
}

}  // namespace chessclaw
