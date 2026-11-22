// ChessClaw - evaluate.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include "position.h"

namespace chessclaw {
// Tapered handcrafted eval (centipawns, White perspective positive).
// Blend of material + piece-square tables, phased by non-pawn material.
int evaluate_hce(const Position& pos);
// Main eval: NNUE if weights loaded, else HCE. Returned from side-to-move view.
int evaluate(const Position& pos);
struct NNUE;
void chessclaw_set_nnue(NNUE* n);
}  // namespace chessclaw
