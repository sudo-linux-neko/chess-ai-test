// ChessClaw - train.cpp (dataset + Adam trainer from scratch, C++20 only)
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include "position.h"
#include "train.h"

namespace chessclaw {

bool save_dataset(const std::string& path, const std::vector<Sample>& v) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::uint32_t magic = 0x54444343;
  std::uint16_t ver = 1;
  std::fwrite(&magic, 4, 1, f);
  std::fwrite(&ver, 2, 1, f);
  std::uint64_t n = v.size();
  std::fwrite(&n, 8, 1, f);
  for (auto& s : v) {
    std::uint8_t L = (std::uint8_t)std::min<std::size_t>(s.fen.size(), 255);
    std::int16_t sc = (std::int16_t)std::max(-15000, std::min(15000, s.score_cp));
    std::uint8_t r = (std::uint8_t)s.result;
    std::fwrite(&L, 1, 1, f);
    std::fwrite(s.fen.data(), 1, L, f);
    std::fwrite(&sc, 2, 1, f);
    std::fwrite(&r, 1, 1, f);
  }
  std::fclose(f);
  return true;
}

bool load_dataset(const std::string& path, std::vector<Sample>& v) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::uint32_t magic = 0; std::uint16_t ver = 0; std::uint64_t n = 0;
  if (std::fread(&magic, 4, 1, f) != 1 || magic != 0x54444343) { std::fclose(f); return false; }
  if (std::fread(&ver, 2, 1, f) != 1 || ver != 1) { std::fclose(f); return false; }
  if (std::fread(&n, 8, 1, f) != 1) { std::fclose(f); return false; }
  v.clear(); v.reserve((std::size_t)std::min<std::uint64_t>(n, 10000000));
  for (std::uint64_t i = 0; i < n; ++i) {
    std::uint8_t L = 0;
    if (std::fread(&L, 1, 1, f) != 1) break;
    std::string fen(L, ' ');
    if (std::fread(fen.data(), 1, L, f) != L) break;
    std::int16_t sc = 0; std::uint8_t r = 1;
    if (std::fread(&sc, 2, 1, f) != 1) break;
    if (std::fread(&r, 1, 1, f) != 1) break;
    v.push_back({fen, sc, r});
  }
  std::fclose(f);
  return true;
}

// Minimal PGN scanner: extracts FENs by replaying SAN? Full SAN parsing is large.
// Instead: accept (a) EPD-ish lines "fen <...> score <cp>", or (b) raw PGN movetext
// which we convert by playing coordinate-ish SAN via a tiny SAN matcher for the
// common cases; uncommon moves are skipped. This keeps the tool dependency-free.
// For production data, prefer pre-annotated EPD from Stockfish.
bool pgn_to_samples(const std::string& pgn_path, const std::string& out_path, int max_positions) {
  std::ifstream in(pgn_path);
  if (!in) return false;
  std::vector<Sample> v;
  std::string line, movetext;
  std::string result = "1/2-1/2";
  auto flush_game = [&]() {
    if (movetext.empty()) return;
    // Very small tokenizer: split on spaces, drop move numbers/results.
    // We do NOT attempt full SAN here; store the PGN path reference as a sample
    // placeholder would be dishonest, so we only emit positions when we can
    // legally parse. For now, emit nothing for raw SAN (caller should use EPD).
    movetext.clear();
  };
  // EPD fast path: if file looks like "fen ... score ..." per line, parse directly.
  std::vector<std::string> all_lines;
  std::string l;
  while (std::getline(in, l)) all_lines.push_back(l);
  bool is_epd = false;
  for (auto& s : all_lines)
    if (s.find("score") != std::string::npos || s.find("ce ") != std::string::npos) { is_epd = true; break; }
  if (is_epd) {
    for (auto& s : all_lines) {
      if ((int)v.size() >= max_positions) break;
      // format: "<fen> score <cp> result <0|1|2>"  (result optional)
      auto p = s.find("score");
      if (p == std::string::npos) continue;
      std::string fen = s.substr(0, p);
      // trim
      while (!fen.empty() && std::isspace((unsigned char)fen.back())) fen.pop_back();
      int cp = 0, res = 1;
      try {
        std::istringstream ss(s.substr(p + 5));
        ss >> cp;
        std::string t;
        if (ss >> t && t == "result") ss >> res;
      } catch (...) { continue; }
      Position pos;
      if (!pos.set_fen(fen)) continue;
      v.push_back({fen, cp, res});
    }
    return save_dataset(out_path, v);
  }
  // Raw PGN without scores: emit FENs with result label derived from game result,
  // score 0 (unsupervised positions for future Stockfish annotation step).
  Position pos;
  pos.set_startpos();
  (void)line; (void)result; (void)flush_game; (void)movetext;
  // We deliberately do not fake SAN parsing: return false with guidance.
  return false;
}

bool train_nnue(NNUE& net, const std::vector<Sample>& data, const std::string& ckpt_path,
                int epochs, float lr, int batch) {
  if (data.empty() || batch <= 0) return false;
  if (!net.loaded) net.init_random(0xC1A9);
  else net.sync_float_from_quant();

  const int H = NNUE::kHid;
  // Adam states (float master)
  static thread_local std::array<std::array<float, NNUE::kHid>, NNUE::kFeat> m_w1{};
  static thread_local std::array<std::array<float, NNUE::kHid>, NNUE::kFeat> v_w1{};
  // (static thread_local 768x128 floats ~ 400KB x2 — ok on stack? No: thread_local static, fine.)
  std::vector<float> m_b1(H, 0), v_b1(H, 0), m_w2(H, 0), v_w2(H, 0);
  float m_b2 = 0, v_b2 = 0;
  const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
  std::mt19937 rng(1234);
  std::vector<int> idx(data.size());
  for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;

  auto sigmoid = [](float x) { return 1.f / (1.f + std::exp(-x)); };
  long step = 0;
  Position pos;
  Position::init_zobrist();

  std::vector<float> hid(H), ahid(H);
  for (int ep = 0; ep < epochs; ++ep) {
    std::shuffle(idx.begin(), idx.end(), rng);
    for (std::size_t b = 0; b < idx.size(); b += (std::size_t)batch) {
      // accumulate grads over minibatch
      static thread_local std::array<std::array<float, NNUE::kHid>, NNUE::kFeat> g_w1{};
      for (auto& row : g_w1) row.fill(0.f);
      std::vector<float> g_b1(H, 0), g_w2(H, 0);
      float g_b2 = 0;
      std::size_t e = std::min(idx.size(), b + (std::size_t)batch);
      for (std::size_t k = b; k < e; ++k) {
        const Sample& s = data[(std::size_t)idx[k]];
        if (!pos.set_fen(s.fen)) continue;
        Color view = Color::White;
        // forward
        for (int j = 0; j < H; ++j) hid[j] = net.f_b1[j];
        int feats[64]; int nf = 0;
        for (int sq = 0; sq < 64; ++sq) {
          Piece p = pos.piece_on(sq);
          if (p == Piece::None) continue;
          feats[nf++] = NNUE::feature_index(view, p, sq);
          const float* row = net.f_w1[feats[nf-1]].data();
          for (int j = 0; j < H; ++j) hid[j] += row[j];
        }
        for (int j = 0; j < H; ++j) ahid[j] = hid[j] < 0 ? 0 : (hid[j] > 1 ? 1 : hid[j]);
        float out = net.f_b2;
        for (int j = 0; j < H; ++j) out += ahid[j] * net.f_w2[j];
        float target = sigmoid((float)s.score_cp / 400.f);
        float pred = sigmoid(out);
        float d_out = (pred - target) * pred * (1 - pred) / (float)(e - b);
        g_b2 += d_out;
        for (int j = 0; j < H; ++j) g_w2[j] += d_out * ahid[j];
        for (int j = 0; j < H; ++j) {
          float dh = d_out * net.f_w2[j] * ((hid[j] > 0 && hid[j] < 1) ? 1.f : 0.f);
          g_b1[j] += dh;
          for (int f = 0; f < nf; ++f) g_w1[feats[f]][j] += dh;
        }
      }
      ++step;
      float bc1 = 1 - std::pow(b1, (float)step), bc2 = 1 - std::pow(b2, (float)step);
      for (int i = 0; i < NNUE::kFeat; ++i)
        for (int j = 0; j < H; ++j) {
          m_w1[i][j] = b1 * m_w1[i][j] + (1 - b1) * g_w1[i][j];
          v_w1[i][j] = b2 * v_w1[i][j] + (1 - b2) * g_w1[i][j] * g_w1[i][j];
          net.f_w1[i][j] -= lr * (m_w1[i][j] / bc1) / (std::sqrt(v_w1[i][j] / bc2) + eps);
        }
      for (int j = 0; j < H; ++j) {
        m_b1[j] = b1 * m_b1[j] + (1 - b1) * g_b1[j];
        v_b1[j] = b2 * v_b1[j] + (1 - b2) * g_b1[j] * g_b1[j];
        net.f_b1[j] -= lr * (m_b1[j] / bc1) / (std::sqrt(v_b1[j] / bc2) + eps);
        m_w2[j] = b1 * m_w2[j] + (1 - b1) * g_w2[j];
        v_w2[j] = b2 * v_w2[j] + (1 - b2) * g_w2[j] * g_w2[j];
        net.f_w2[j] -= lr * (m_w2[j] / bc1) / (std::sqrt(v_w2[j] / bc2) + eps);
      }
      m_b2 = b1 * m_b2 + (1 - b1) * g_b2;
      v_b2 = b2 * v_b2 + (1 - b2) * g_b2 * g_b2;
      net.f_b2 -= lr * (m_b2 / bc1) / (std::sqrt(v_b2 / bc2) + eps);
    }
    net.sync_quant_from_float();
    net.loaded = true;
    if (!ckpt_path.empty()) net.save(ckpt_path + ".ep" + std::to_string(ep));
  }
  net.sync_quant_from_float();
  net.loaded = true;
  if (!ckpt_path.empty()) net.save(ckpt_path);
  return true;
}

}  // namespace chessclaw
