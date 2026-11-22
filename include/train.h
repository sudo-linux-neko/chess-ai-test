// ChessClaw - train.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "nnue.h"

namespace chessclaw {

// Compact binary sample: FEN string + game result/score label.
// Dataset file format (little-endian, portable):
//   u32 magic 'CCDT', u16 version=1, then repeated records:
//   u8 fen_len, char fen[fen_len], i16 score_cp (white perspective), u8 result (0 loss,1 draw,2 win)
struct Sample {
  std::string fen;
  int score_cp = 0;
  int result = 1;
};

bool save_dataset(const std::string& path, const std::vector<Sample>& v);
bool load_dataset(const std::string& path, std::vector<Sample>& v);
// Convert Lichess PGN (or EPD lines "fen score") into samples. Pure C++20, no deps.
bool pgn_to_samples(const std::string& pgn_path, const std::string& out_path, int max_positions);
// Train float master weights with Adam from scratch; periodically quantize+save.
// epochs, lr, batch configurable; checkpoint_mb guarded for the 10GB budget.
bool train_nnue(NNUE& net, const std::vector<Sample>& data, const std::string& ckpt_path,
                int epochs = 4, float lr = 0.001f, int batch = 1024);

}  // namespace chessclaw
