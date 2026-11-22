// ChessClaw - nnue.cpp (tiny 768x128x1, incremental + Adam training support)
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "nnue.h"
#include <cmath>
#include <cstdio>
#include <random>

namespace chessclaw {

void NNUE::init_random(unsigned seed) {
  std::mt19937 rng(seed ? seed : 0xC1A9);
  std::uniform_real_distribution<float> d(-0.05f, 0.05f);
  for (auto& row : f_w1) for (auto& w : row) w = d(rng);
  for (auto& b : f_b1) b = d(rng);
  for (auto& w : f_w2) w = d(rng);
  f_b2 = 0.f;
  sync_quant_from_float();
  loaded = true;
}

void NNUE::sync_quant_from_float() {
  for (int i = 0; i < kFeat; ++i)
    for (int j = 0; j < kHid; ++j) {
      float v = f_w1[i][j] * kQA;
      if (v > 127) v = 127; if (v < -128) v = -128;
      w1[i][j] = (std::int16_t)std::lround(v);
    }
  for (int j = 0; j < kHid; ++j) {
    float v = f_b1[j] * kQA;
    if (v > 30000) v = 30000; if (v < -30000) v = -30000;
    b1[j] = (std::int16_t)std::lround(v);
    float w = f_w2[j] * kQB;
    if (w > 127) w = 127; if (w < -128) w = -128;
    w2[j] = (std::int16_t)std::lround(w);
  }
}

void NNUE::sync_float_from_quant() {
  for (int i = 0; i < kFeat; ++i)
    for (int j = 0; j < kHid; ++j) f_w1[i][j] = (float)w1[i][j] / kQA;
  for (int j = 0; j < kHid; ++j) {
    f_b1[j] = (float)b1[j] / kQA;
    f_w2[j] = (float)w2[j] / kQB;
  }
}

bool NNUE::save(const std::string& path) const {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::uint32_t magic = 0x454E4343;  // 'CCNE'
  std::uint16_t ver = 1;
  std::fwrite(&magic, 4, 1, f);
  std::fwrite(&ver, 2, 1, f);
  std::fwrite(w1.data(), sizeof(w1), 1, f);
  std::fwrite(b1.data(), sizeof(b1), 1, f);
  std::fwrite(w2.data(), sizeof(w2), 1, f);
  std::fwrite(&b2, sizeof(b2), 1, f);
  // also float master for resume
  std::fwrite(f_w1.data(), sizeof(f_w1), 1, f);
  std::fwrite(f_b1.data(), sizeof(f_b1), 1, f);
  std::fwrite(f_w2.data(), sizeof(f_w2), 1, f);
  std::fwrite(&f_b2, sizeof(f_b2), 1, f);
  std::fclose(f);
  return true;
}

bool NNUE::load(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::uint32_t magic = 0; std::uint16_t ver = 0;
  if (std::fread(&magic, 4, 1, f) != 1 || magic != 0x454E4343) { std::fclose(f); return false; }
  if (std::fread(&ver, 2, 1, f) != 1 || ver != 1) { std::fclose(f); return false; }
  bool ok = true;
  ok &= std::fread(w1.data(), sizeof(w1), 1, f) == 1;
  ok &= std::fread(b1.data(), sizeof(b1), 1, f) == 1;
  ok &= std::fread(w2.data(), sizeof(w2), 1, f) == 1;
  ok &= std::fread(&b2, sizeof(b2), 1, f) == 1;
  if (std::fread(f_w1.data(), sizeof(f_w1), 1, f) == 1) {
    ok &= std::fread(f_b1.data(), sizeof(f_b1), 1, f) == 1;
    ok &= std::fread(f_w2.data(), sizeof(f_w2), 1, f) == 1;
    ok &= std::fread(&f_b2, sizeof(f_b2), 1, f) == 1;
  } else {
    sync_float_from_quant();
  }
  std::fclose(f);
  loaded = ok;
  return ok;
}

int NNUE::feature_index(Color view, Piece p, int sq) {
  int pc = piece_idx(p);  // 0..11
  int rel = (view == Color::White) ? sq : (sq ^ 56);
  int side = (color_of(p) == view) ? 0 : 1;
  int pt = (int)type_of(p);  // 0..5
  return side * 384 + pt * 64 + rel;
}

void NNUE::features_of(const Position& pos, Color view, int* idx, int& n) const {
  n = 0;
  for (int sq = 0; sq < 64; ++sq) {
    Piece p = pos.piece_on(sq);
    if (p == Piece::None) continue;
    idx[n++] = feature_index(view, p, sq);
  }
}

float NNUE::forward_float(const Position& pos, Color view) const {
  float hid[kHid];
  for (int j = 0; j < kHid; ++j) hid[j] = f_b1[j];
  for (int sq = 0; sq < 64; ++sq) {
    Piece p = pos.piece_on(sq);
    if (p == Piece::None) continue;
    int fi = feature_index(view, p, sq);
    const float* row = f_w1[fi].data();
    for (int j = 0; j < kHid; ++j) hid[j] += row[j];
  }
  float out = f_b2;
  for (int j = 0; j < kHid; ++j) {
    float a = hid[j] < 0 ? 0 : (hid[j] > 1 ? 1 : hid[j]);
    out += a * f_w2[j];
  }
  return out;  // scaled: sigmoid(out) -> win prob; training maps cp via sigmoid
}

int NNUE::infer(const Position& pos) const {
  // Quantized inference with auto-vectorizable loop.
  std::int32_t acc[kHid];
  for (int j = 0; j < kHid; ++j) acc[j] = b1[j];
  for (int sq = 0; sq < 64; ++sq) {
    Piece p = pos.piece_on(sq);
    if (p == Piece::None) continue;
    int fi = feature_index(pos.side_to_move(), p, sq);
    const std::int16_t* row = w1[fi].data();
    for (int j = 0; j < kHid; ++j) acc[j] += row[j];
  }
  std::int32_t out = 0;
  for (int j = 0; j < kHid; ++j) {
    std::int32_t a = acc[j] < 0 ? 0 : (acc[j] > kQA ? kQA : acc[j]);
    out += (a * w2[j]) / kQA;
  }
  out = out / kQB + b2;
  // out is in ~pawn*? scale to centipawns: calibrated so sigmoid-ish range maps
  // to [-1500,1500]. Keep linear scale factor 2 for now (tuned during training).
  if (out > 1500) out = 1500;
  if (out < -1500) out = -1500;
  return (int)(out * 2);
}

void Accumulator::refresh(const NNUE& net, const Position& pos, Color view) {
  for (int j = 0; j < NNUE::kHid; ++j) v[j] = net.b1[j];
  for (int sq = 0; sq < 64; ++sq) {
    Piece p = pos.piece_on(sq);
    if (p == Piece::None) continue;
    add_feature(net, NNUE::feature_index(view, p, sq));
  }
}
void Accumulator::add_feature(const NNUE& net, int feat) {
  const auto& row = net.w1[feat];
  for (int j = 0; j < NNUE::kHid; ++j) v[j] += row[j];
}
void Accumulator::sub_feature(const NNUE& net, int feat) {
  const auto& row = net.w1[feat];
  for (int j = 0; j < NNUE::kHid; ++j) v[j] -= row[j];
}
int Accumulator::output(const NNUE& net) const {
  std::int32_t out = 0;
  for (int j = 0; j < NNUE::kHid; ++j) {
    std::int32_t a = v[j] < 0 ? 0 : (v[j] > NNUE::kQA ? NNUE::kQA : v[j]);
    out += (a * net.w2[j]) / NNUE::kQA;
  }
  out = out / NNUE::kQB + net.b2;
  if (out > 1500) out = 1500;
  if (out < -1500) out = -1500;
  return (int)(out * 2);
}

}  // namespace chessclaw
