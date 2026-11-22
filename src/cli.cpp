// ChessClaw - cli.cpp (portable terminal UI, Unicode w/ ASCII fallback)
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "evaluate.h"
#include "movegen.h"
#include "search.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace chessclaw {
namespace {

bool use_unicode() {
  if (std::getenv("CHESSCLAW_ASCII")) return false;
#ifdef _WIN32
  return true;  // VT mode enabled below; fallback still available via env
#else
  const char* lang = std::getenv("LANG");
  const char* lc = std::getenv("LC_ALL");
  std::string s = std::string(lc ? lc : "") + std::string(lang ? lang : "");
  if (s.find("UTF-8") != std::string::npos || s.find("utf8") != std::string::npos) return true;
  // xterm-256color on modern systems is UTF-8 capable; default to unicode
  // unless explicitly ASCII.
  return true;
#endif
}

std::string piece_glyph(Piece p, bool uni) {
  if (!uni) {
    if (p == Piece::None) return ".";
    const char* n = "PNBRQKpnbrqk";
    return std::string(1, n[piece_idx(p)]);
  }
  switch (p) {
    case Piece::WK: return "\u2654"; case Piece::WQ: return "\u2655";
    case Piece::WR: return "\u2656"; case Piece::WB: return "\u2657";
    case Piece::WN: return "\u2658"; case Piece::WP: return "\u2659";
    case Piece::BK: return "\u265A"; case Piece::BQ: return "\u265B";
    case Piece::BR: return "\u265C"; case Piece::BB: return "\u265D";
    case Piece::BN: return "\u265E"; case Piece::BP: return "\u265F";
    default: return ".";
  }
}

void print_board(const Position& pos) {
  bool uni = use_unicode();
  std::cout << "\n  +-----------------+\n";
  for (int r = 7; r >= 0; --r) {
    std::cout << (r + 1) << " | ";
    for (int f = 0; f < 8; ++f) {
      Piece p = pos.piece_on(make_sq(f, r));
      bool dark = (f + r) % 2 == 0;
      const char* bg = dark ? "\x1b[48;5;239m" : "\x1b[48;5;250m";
      const char* fg = (p != Piece::None && color_of(p) == Color::Black) ? "\x1b[30m" : "\x1b[97m";
      std::cout << bg << fg << piece_glyph(p, uni) << " \x1b[0m";
    }
    std::cout << "|\n";
  }
  std::cout << "  +-----------------+\n    a b c d e f g h\n";
  std::cout << "FEN: " << pos.to_fen() << "\n";
}

}  // namespace

void cli_loop() {
#ifdef _WIN32
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h != INVALID_HANDLE_VALUE) {
    DWORD m = 0;
    if (GetConsoleMode(h, &m)) SetConsoleMode(h, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#endif
  Position::init_zobrist();
  init_attacks();
  Position pos;
  Searcher searcher;
  searcher.set_threads(2);
  searcher.set_hash_mb(64);

  struct Hist { Move m; Undo u; std::string fen_before; };
  std::vector<Hist> hist;

  Color human = Color::White;
  std::cout << "ChessClaw CLI. Type 'help' for commands.\n";
  std::cout << "Choose color (w/b) [w]: ";
  std::string c;
  if (std::getline(std::cin, c) && (c == "b" || c == "B")) human = Color::Black;

  int movetime = 800;
  std::cout << "Engine movetime ms [800]: ";
  std::string t;
  if (std::getline(std::cin, t) && !t.empty()) {
    try { movetime = std::stoi(t); } catch (...) {}
  }

  for (;;) {
    print_board(pos);
    MoveList ml; generate_legal(pos, ml);
    if (ml.size == 0) {
      std::cout << (pos.in_check() ? "Checkmate. " : "Stalemate. ");
      std::cout << (pos.side_to_move() == human ? "You have no moves.\n" : "Engine has no moves.\n");
      std::cout << "Type 'new' or 'quit'.\n";
    }
    if (pos.side_to_move() != human && ml.size > 0) {
      Limits lim; lim.movetime_ms = movetime; lim.depth = 64;
      SearchResult r = searcher.go(pos, lim);
      Undo u; pos.make_move(r.best, u);
      hist.push_back({r.best, u, ""});
      std::cout << "Engine plays " << r.best.to_uci() << " (cp " << r.score << ", d" << r.depth << ")\n";
      continue;
    }
    std::cout << (pos.in_check() ? "Your move (CHECK) > " : "Your move > ");
    std::string line;
    if (!std::getline(std::cin, line)) break;
    if (line == "quit" || line == "exit" || line == "resign") {
      std::cout << "Thanks for playing ChessClaw.\n";
      break;
    }
    if (line == "help") {
      std::cout << "Commands: <e2e4|hint|undo|new|fen|ascii|quit>. Moves in UCI (e2e4, e7e8q).\n";
      continue;
    }
    if (line == "hint") {
      Limits lim; lim.movetime_ms = movetime; lim.depth = 64;
      SearchResult r = searcher.go(pos, lim);
      std::cout << "Hint: " << r.best.to_uci() << "\n";
      continue;
    }
    if (line == "undo") {
      if (!hist.empty()) {
        // undo engine + human if possible
        for (int k = 0; k < 2 && !hist.empty(); ++k) {
          Hist h = hist.back(); hist.pop_back();
          pos.unmake_move(h.m, h.u);
          if (pos.side_to_move() == human) break;
        }
      } else std::cout << "Nothing to undo.\n";
      continue;
    }
    if (line == "new") { pos.set_startpos(); hist.clear(); continue; }
    if (line == "fen") { std::cout << pos.to_fen() << "\n"; continue; }
    if (line == "ascii") {
#ifdef _WIN32
      _putenv("CHESSCLAW_ASCII=1");
#else
      setenv("CHESSCLAW_ASCII", "1", 1);
#endif
      continue;
    }
    if (line.rfind("draw", 0) == 0) { std::cout << "Draw offer noted (engine accepts only stalemate/50-move).\n"; continue; }
    Move want = Move::from_uci(line);
    bool ok = false;
    for (int i = 0; i < ml.size; ++i) {
      if (ml.moves[i].from == want.from && ml.moves[i].to == want.to && ml.moves[i].promo == want.promo) {
        Undo u; pos.make_move(ml.moves[i], u);
        hist.push_back({ml.moves[i], u, ""});
        ok = true;
        break;
      }
    }
    if (!ok) std::cout << "Illegal move. Try e.g. e2e4.\n";
  }
}

}  // namespace chessclaw
