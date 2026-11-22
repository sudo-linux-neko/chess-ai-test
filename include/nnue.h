// ChessClaw - nnue.h
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
//
// Tiny CPU-friendly network used as the learnable eval component:
//   features: 768 (2 colors x 6 pieces x 64 squares), 0/1 binary
//   hidden:   H=128 with clipped-ReLU
//   output:   1 sigmoid-scaled to centipawns
// Weights stored int16 quantized (QA=255 scale) for SIMD-friendly inference;
// training keeps a float master copy and re-quantizes on save.
// Incremental update: accumulator add/sub of input-weight rows on make/unmake.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "position.h"

namespace chessclaw {

struct NNUE {
  static constexpr int kFeat = 768;
  static constexpr int kHid = 128;
  static constexpr int kQA = 255;
  static constexpr int kQB = 64;

  // Quantized inference weights
  std::array<std::array<std::int16_t, kHid>, kFeat> w1{};
  std::array<std::int16_t, kHid> b1{};
  std::array<std::int16_t, kHid> w2{};
  std::int32_t b2 = 0;
  bool loaded = false;

  // Float master copy for training
  std::array<std::array<float, kHid>, kFeat> f_w1{};
  std::array<float, kHid> f_b1{};
  std::array<float, kHid> f_w2{};
  float f_b2 = 0.f;

  void init_random(unsigned seed = 0xC1A9);
  bool load(const std::string& path);
  bool save(const std::string& path) const;
  void sync_quant_from_float();
  void sync_float_from_quant();

  // Feature index: stm-relative HalfKA-lite: side * 384 + piece*64 + sq(rel)
  static int feature_index(Color view, Piece p, int sq);
  void features_of(const Position& pos, Color view, int* idx, int& n) const;

  // Full forward (float, for training) and quantized inference.
  float forward_float(const Position& pos, Color view) const;
  int infer(const Position& pos) const;  // centipawns, stm perspective
};

// Fixed-size incremental accumulator stack (one per search ply is overkill;
// we keep a small stack owned by search; helpers here).
struct Accumulator {
  std::array<std::int32_t, NNUE::kHid> v{};
  void refresh(const NNUE& net, const Position& pos, Color view);
  void add_feature(const NNUE& net, int feat);
  void sub_feature(const NNUE& net, int feat);
  int output(const NNUE& net) const;
};

}  // namespace chessclaw
