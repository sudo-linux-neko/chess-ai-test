// ChessClaw - uci.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include <iostream>
#include <sstream>
#include <thread>
#include "evaluate.h"
#include "movegen.h"
#include "search.h"
#include "uci.h"

namespace chessclaw {

static NNUE g_nnue;

void uci_loop() {
  Position::init_zobrist();
  init_attacks();
  chessclaw_set_nnue(&g_nnue);
  Position pos;
  Searcher searcher;
  searcher.set_nnue(&g_nnue);
  std::string line;
  int threads = 1;
  // rough core default: hardware_concurrency clamped
  {
    unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) hc = 1;
    if (hc > 12) hc = 12;
    threads = (int)hc >= 4 ? 4 : (int)hc;  // conservative default for 7.6GB RAM
    searcher.set_threads(threads);
  }
  while (std::getline(std::cin, line)) {
    std::istringstream ss(line);
    std::string cmd; ss >> cmd;
    if (cmd == "uci") {
      std::cout << "id name ChessClaw 0.1.0\nid author Vaubhav\n";
      std::cout << "option name Threads type spin default " << threads << " min 1 max 64\n";
      std::cout << "option name Hash type spin default 64 min 1 max 1024\n";
      std::cout << "option name Weights type string default weights/nnue.bin\n";
      std::cout << "uciok\n";
    } else if (cmd == "isready") {
      std::cout << "readyok\n";
    } else if (cmd == "ucinewgame") {
      searcher.clear();
      pos.set_startpos();
    } else if (cmd == "setoption") {
      std::string dummy, name, value;
      ss >> dummy >> name;  // "name <X>"
      std::string opt = name;
      // multi-word parse: "setoption name Hash value 128"
      std::string tok;
      std::string val;
      bool invalue = false;
      while (ss >> tok) {
        if (tok == "value") { invalue = true; continue; }
        if (!invalue) opt += " " + tok;
        else { if (!val.empty()) val += " "; val += tok; }
      }
      // trim "name " prefix handling: our first read consumed "name"
      if (opt.rfind("Threads", 0) != std::string::npos || opt == "Threads") {
        try { searcher.set_threads(std::stoi(val)); } catch (...) {}
      } else if (opt.find("Hash") != std::string::npos) {
        try { searcher.set_hash_mb((std::size_t)std::stoul(val)); } catch (...) {}
      } else if (opt.find("Weights") != std::string::npos) {
        if (!val.empty() && g_nnue.load(val)) {}
      }
    } else if (cmd == "position") {
      std::string tok; ss >> tok;
      if (tok == "startpos") {
        pos.set_startpos();
        ss >> tok;  // "moves" or EOF
      } else if (tok == "fen") {
        std::string fen;
        std::string part;
        for (int i = 0; i < 6 && ss >> part;) {
          if (part == "moves") break;
          if (!fen.empty()) fen += " ";
          fen += part;
          // FEN has 6 fields; detect "moves"
          int spaces = 0; for (char c : fen) if (c == ' ') spaces++;
          if (spaces >= 5) { /* may include next token check */ }
          i++;
          // peek: if part was moves handled above
        }
        // The loop above is fragile; reparse from raw line instead:
        auto p = line.find("fen ");
        auto m = line.find(" moves");
        std::string fens = (p == std::string::npos) ? "" : line.substr(p + 4, (m == std::string::npos ? std::string::npos : m - (p + 4)));
        if (!fens.empty()) pos.set_fen(fens);
        tok = (m == std::string::npos) ? "" : "moves";
        if (tok != "moves") {
          // check remaining tokens after fen for moves
          // simplest: if " moves" present, apply them below
        }
      }
      // apply moves (scan raw line for "moves")
      auto mp = line.find(" moves");
      if (mp != std::string::npos) {
        std::istringstream ms(line.substr(mp + 7));
        std::string u;
        while (ms >> u) {
          Move m = Move::from_uci(u);
          // enrich flags by matching legal moves
          MoveList ml; generate_legal(pos, ml);
          bool applied = false;
          for (int i = 0; i < ml.size; ++i) {
            if (ml.moves[i].from == m.from && ml.moves[i].to == m.to && ml.moves[i].promo == m.promo) {
              Undo un; pos.make_move(ml.moves[i], un);
              (void)un;  // history ignored in UCI path (no takeback)
              applied = true;
              break;
            }
          }
          if (!applied) break;
        }
      }
    } else if (cmd == "go") {
      Limits lim;
      std::string tok;
      int depth = 64;
      bool has_depth = false;
      while (ss >> tok) {
        if (tok == "depth" && (ss >> tok)) { depth = std::stoi(tok); has_depth = true; }
        else if (tok == "movetime" && (ss >> tok)) lim.movetime_ms = std::stoll(tok);
        else if (tok == "wtime" && (ss >> tok)) lim.wtime_ms = std::stoll(tok);
        else if (tok == "btime" && (ss >> tok)) lim.btime_ms = std::stoll(tok);
        else if (tok == "winc" && (ss >> tok)) lim.winc_ms = std::stoll(tok);
        else if (tok == "binc" && (ss >> tok)) lim.binc_ms = std::stoll(tok);
        else if (tok == "infinite") lim.infinite = true;
      }
      lim.depth = depth;
      lim.use_clock = (lim.wtime_ms > 0 || lim.btime_ms > 0);
      if (!has_depth && lim.movetime_ms == 0 && !lim.use_clock && !lim.infinite) lim.depth = 8;
      SearchResult r = searcher.go(pos, lim);
      std::cout << "info depth " << r.depth << " score cp " << r.score
                << " nodes " << r.nodes << "\n";
      std::cout << "bestmove " << r.best.to_uci() << "\n";
    } else if (cmd == "stop") {
      searcher.stop();
    } else if (cmd == "quit") {
      break;
    }
    std::cout.flush();
  }
}

}  // namespace chessclaw
