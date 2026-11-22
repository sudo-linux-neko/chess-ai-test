// ChessClaw - movegen.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include <cstdint>
#include "position.h"
#include "types.h"

namespace chessclaw {

struct MoveList {
  static constexpr int kMax = 256;
  Move moves[kMax]{};
  int size = 0;
  void push(Move m) { if (size < kMax) moves[size++] = m; }
  void clear() { size = 0; }
};

// Fully legal move generation (pseudo-legal filtered by king safety).
void generate_legal(const Position& pos, MoveList& out);
void generate_captures(const Position& pos, MoveList& out);  // legal captures (+promos) for quiescence
bool has_legal(const Position& pos);
U64 perft(Position& pos, int depth);

}  // namespace chessclaw
