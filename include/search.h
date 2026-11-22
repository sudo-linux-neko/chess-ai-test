// ChessClaw - search.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>
#include "nnue.h"
#include "position.h"
#include "tt.h"
#include "types.h"

// Forward declare to avoid header cycle (movegen.h includes position.h).
namespace chessclaw { struct MoveList; }

namespace chessclaw {

struct Limits {
  int depth = 64;              // max depth if no time limit
  long long movetime_ms = 0;   // fixed time per move
  long long wtime_ms = 0, btime_ms = 0, winc_ms = 0, binc_ms = 0;
  bool infinite = false;
  bool use_clock = false;
};

struct SearchResult {
  Move best{};
  Score score = 0;
  int depth = 0;
  U64 nodes = 0;
};

// Lazy SMP searcher: N worker threads run iterative deepening on a shared TT;
// main thread keeps the best completed iteration. Stop via atomic flag + deadline.
class Searcher {
 public:
  Searcher();
  void set_threads(int n);
  void set_hash_mb(std::size_t mb);
  void set_nnue(NNUE* n) { nnue_ = n; }
  void clear();
  SearchResult go(Position pos, const Limits& lim);
  void stop() { stop_.store(true); }
  U64 nodes() const { return nodes_.load(); }

 private:
  TranspositionTable tt_;
  NNUE* nnue_ = nullptr;
  int threads_ = 1;
  std::atomic<bool> stop_{false};
  std::atomic<U64> nodes_{0};
  std::chrono::steady_clock::time_point deadline_{};
  bool timed_ = false;

  bool out_of_time() const;

  // Per-thread context (killers/history/counter are thread-local).
  struct Ctx {
    Move killers[64][2]{};
    int history[2][64][64]{};
    Move counter[12][64]{};
    int lmr_table[64][64]{};
    U64 nodes = 0;
  };

  SearchResult iterate(Position& pos, const Limits& lim, int thread_id);
  Score pv_search(Position& pos, Ctx& ctx, int depth, Score alpha, Score beta,
                  int ply, bool pv_node, Move& out_best, std::vector<U64>& rep);
  Score quiesce(Position& pos, Ctx& ctx, Score alpha, Score beta, int ply);
  Score see(const Position& pos, Move m) const;
  void order_moves(const Position& pos, Ctx& ctx, MoveList& ml, Move tt_move, int ply,
                   const Move& prev, Piece prev_pc) const;
};

}  // namespace chessclaw
