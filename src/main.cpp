// ChessClaw - main.cpp
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include <iostream>
#include <string>
#include "movegen.h"
#include "position.h"
#include "search.h"
#include "train.h"
#include "uci.h"

using namespace chessclaw;

static void print_help() {
  std::cout << "ChessClaw 0.1.0 by Vaubhav (GPLv3)\n"
               "Usage:\n"
               "  chessclaw               UCI engine (default)\n"
               "  chessclaw --cli         human-vs-engine terminal UI\n"
               "  chessclaw --perft D     perft from startpos\n"
               "  chessclaw --bench       quick search bench\n"
               "  chessclaw --train DATA OUT [epochs]   train tiny NNUE (C++ Adam)\n"
               "  chessclaw --help        this help\n";
}

static void run_perft(int d) {
  Position::init_zobrist();
  init_attacks();
  Position p;
  U64 n = perft(p, d);
  std::cout << "perft(" << d << ") = " << n << "\n";
}

static void run_bench() {
  Position::init_zobrist();
  init_attacks();
  Position p;
  p.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  Searcher s;
  s.set_threads(1);
  s.set_hash_mb(16);
  Limits lim; lim.depth = 6;
  SearchResult r = s.go(p, lim);
  std::cout << "bench: best=" << r.best.to_uci() << " score=" << r.score
            << " depth=" << r.depth << " nodes=" << r.nodes << "\n";
}

int main(int argc, char** argv) {
  if (argc >= 2) {
    std::string a = argv[1];
    if (a == "--help" || a == "-h") { print_help(); return 0; }
    if (a == "--cli") { cli_loop(); return 0; }
    if (a == "--perft" && argc >= 3) { run_perft(std::stoi(argv[2])); return 0; }
    if (a == "--bench") { run_bench(); return 0; }
    if (a == "--train" && argc >= 4) {
      std::vector<Sample> data;
      if (!load_dataset(argv[2], data)) { std::cerr << "cannot load dataset\n"; return 1; }
      int epochs = argc >= 5 ? std::stoi(argv[4]) : 4;
      NNUE net; net.init_random(7);
      std::string out = argv[3];
      if (!train_nnue(net, data, out, epochs)) { std::cerr << "train failed\n"; return 1; }
      std::cout << "trained on " << data.size() << " samples -> " << out << "\n";
      return 0;
    }
    if (a == "--uci") { uci_loop(); return 0; }
  }
  uci_loop();
  return 0;
}
