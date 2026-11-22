// ChessClaw - bitboard.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "bitboard.h"

namespace chessclaw {
namespace {
U64 kKnight[64]{}, kKing[64]{}, kPawn[2][64]{};
bool kInit = false;
}  // namespace

void init_attacks() {
  if (kInit) return;
  for (int sq = 0; sq < 64; ++sq) {
    int f = file_of(sq), r = rank_of(sq);
    U64 n = 0, k = 0;
    const int nd[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
    for (auto& d : nd) {
      int nf = f + d[0], nr = r + d[1];
      if (on_board(nf, nr)) n |= sq_bb(make_sq(nf, nr));
    }
    for (int df = -1; df <= 1; ++df)
      for (int dr = -1; dr <= 1; ++dr) {
        if (!df && !dr) continue;
        int nf = f + df, nr = r + dr;
        if (on_board(nf, nr)) k |= sq_bb(make_sq(nf, nr));
      }
    kKnight[sq] = n;
    kKing[sq] = k;
    // White pawns attack r+1, black r-1
    U64 w = 0, b = 0;
    if (r + 1 < 8) {
      if (f - 1 >= 0) w |= sq_bb(make_sq(f - 1, r + 1));
      if (f + 1 < 8) w |= sq_bb(make_sq(f + 1, r + 1));
    }
    if (r - 1 >= 0) {
      if (f - 1 >= 0) b |= sq_bb(make_sq(f - 1, r - 1));
      if (f + 1 < 8) b |= sq_bb(make_sq(f + 1, r - 1));
    }
    kPawn[0][sq] = w;
    kPawn[1][sq] = b;
  }
  kInit = true;
}

U64 knight_attacks_of(int sq) { return kKnight[sq]; }
U64 king_attacks_of(int sq) { return kKing[sq]; }
U64 pawn_attacks_of(Color c, int sq) { return kPawn[color_idx(c)][sq]; }

U64 rook_attacks_of(int sq, U64 occ) {
  U64 a = 0;
  int f = file_of(sq), r = rank_of(sq);
  for (int nf = f + 1; nf < 8; ++nf) { int s = make_sq(nf, r); a |= sq_bb(s); if (occ & sq_bb(s)) break; }
  for (int nf = f - 1; nf >= 0; --nf) { int s = make_sq(nf, r); a |= sq_bb(s); if (occ & sq_bb(s)) break; }
  for (int nr = r + 1; nr < 8; ++nr) { int s = make_sq(f, nr); a |= sq_bb(s); if (occ & sq_bb(s)) break; }
  for (int nr = r - 1; nr >= 0; --nr) { int s = make_sq(f, nr); a |= sq_bb(s); if (occ & sq_bb(s)) break; }
  return a;
}

U64 bishop_attacks_of(int sq, U64 occ) {
  U64 a = 0;
  int f = file_of(sq), r = rank_of(sq);
  for (int i = 1;; ++i) {
    int nf = f + i, nr = r + i;
    if (!on_board(nf, nr)) break;
    int s = make_sq(nf, nr); a |= sq_bb(s); if (occ & sq_bb(s)) break;
  }
  for (int i = 1;; ++i) {
    int nf = f + i, nr = r - i;
    if (!on_board(nf, nr)) break;
    int s = make_sq(nf, nr); a |= sq_bb(s); if (occ & sq_bb(s)) break;
  }
  for (int i = 1;; ++i) {
    int nf = f - i, nr = r + i;
    if (!on_board(nf, nr)) break;
    int s = make_sq(nf, nr); a |= sq_bb(s); if (occ & sq_bb(s)) break;
  }
  for (int i = 1;; ++i) {
    int nf = f - i, nr = r - i;
    if (!on_board(nf, nr)) break;
    int s = make_sq(nf, nr); a |= sq_bb(s); if (occ & sq_bb(s)) break;
  }
  return a;
}

}  // namespace chessclaw
