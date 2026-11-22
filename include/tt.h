// ChessClaw - tt.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "types.h"

namespace chessclaw {

enum class TTFlag : std::uint8_t { None = 0, Exact = 1, Lower = 2, Upper = 3 };

// Lockless transposition table: each slot is (key, data) atomics.
// data packs: move(16) | depth(8) | flag(2) | score(16 signed) | age(6).
// A probe is accepted only if stored key == full hash (no false hits from races
// beyond negligible probability; torn reads detected via key mismatch).
class TranspositionTable {
 public:
  TranspositionTable() = default;
  void resize_mb(std::size_t mb);
  void clear();
  std::size_t size() const { return mask_ + 1; }

  struct Found {
    bool hit = false;
    Move move{};
    Score score = 0;
    int depth = -1;
    TTFlag flag = TTFlag::None;
  };
  Found probe(U64 hash) const;
  void store(U64 hash, Move best, Score score, int depth, TTFlag flag);

 private:
  struct Slot {
    std::atomic<U64> key{0};
    std::atomic<U64> data{0};
  };
  std::vector<Slot> t_;
  std::size_t mask_ = 0;

  static U64 pack(Move m, Score s, int depth, TTFlag f);
  static void unpack(U64 d, Move& m, Score& s, int& depth, TTFlag& f);
};

}  // namespace chessclaw
