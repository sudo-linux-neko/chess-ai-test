// ChessClaw - tt.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "tt.h"

namespace chessclaw {

void TranspositionTable::resize_mb(std::size_t mb) {
  std::size_t bytes = mb * 1024 * 1024;
  std::size_t n = bytes / 16;
  std::size_t pow2 = 1;
  while (pow2 * 2 <= n) pow2 *= 2;
  if (pow2 < 1024) pow2 = 1024;
  t_.clear();
  t_ = std::vector<Slot>(pow2);
  mask_ = pow2 - 1;
  clear();
}

void TranspositionTable::clear() {
  for (auto& s : t_) {
    s.key.store(0, std::memory_order_relaxed);
    s.data.store(0, std::memory_order_relaxed);
  }
}

U64 TranspositionTable::pack(Move m, Score s, int depth, TTFlag f) {
  // layout: [0..15] move packed, [16..23] depth, [24..25] flag, [26..41] score+offset, [42..] unused
  U64 su = (U64)(s + INF + 1000) & 0xFFFFu;
  return (U64)m.packed() | ((U64)(depth & 0xFF) << 16) | ((U64)((int)f & 3) << 24) | (su << 26);
}

void TranspositionTable::unpack(U64 d, Move& m, Score& s, int& depth, TTFlag& f) {
  m.from = d & 63; m.to = (d >> 6) & 63; m.promo = (d >> 12) & 15;
  m.capture = false; m.double_push = false; m.enpassant = false;
  m.castle_k = false; m.castle_q = false;
  depth = (int)((d >> 16) & 0xFF);
  f = (TTFlag)((d >> 24) & 3);
  s = (Score)(((d >> 26) & 0xFFFFu)) - INF - 1000;
}

TranspositionTable::Found TranspositionTable::probe(U64 hash) const {
  Found r;
  if (t_.empty()) return r;
  const Slot& sl = t_[hash & mask_];
  U64 k = sl.key.load(std::memory_order_relaxed);
  if (k != hash) return r;
  U64 d = sl.data.load(std::memory_order_relaxed);
  unpack(d, r.move, r.score, r.depth, r.flag);
  r.hit = true;
  return r;
}

void TranspositionTable::store(U64 hash, Move best, Score score, int depth, TTFlag flag) {
  if (t_.empty()) return;
  Slot& sl = t_[hash & mask_];
  // depth-preferred replacement: keep deeper entries.
  U64 old_d = sl.data.load(std::memory_order_relaxed);
  U64 old_k = sl.key.load(std::memory_order_relaxed);
  if (old_k != 0 && old_k != hash) {
    Move om; Score os; int od; TTFlag of;
    unpack(old_d, om, os, od, of);
    if (od > depth + 2) return;
  }
  sl.data.store(pack(best, score, depth, flag), std::memory_order_relaxed);
  sl.key.store(hash, std::memory_order_relaxed);
}

}  // namespace chessclaw
