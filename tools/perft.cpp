// ChessClaw perft tool
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include <iostream>
#include "movegen.h"
#include "position.h"
using namespace chessclaw;
int main() {
  Position::init_zobrist();
  init_attacks();
  Position p;
  // Known startpos perft values.
  const unsigned long long expect[] = {1, 20, 400, 8902, 197281};
  bool ok = true;
  for (int d = 1; d <= 4; ++d) {
    Position q = p;
    unsigned long long n = perft(q, d);
    std::cout << "perft " << d << " = " << n << " (expect " << expect[d] << ")\n";
    if (n != expect[d]) ok = false;
  }
  return ok ? 0 : 1;
}
