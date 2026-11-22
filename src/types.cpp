// ChessClaw - types.cpp (Move UCI helpers)
// GNU GPL v3. Copyright (C) 2026 Vaubhav.
#include "types.h"

namespace chessclaw {

Move Move::from_uci(const std::string& s) {
  Move m{};
  if (s.size() < 4) return m;
  int ff = s[0] - 'a', fr = s[1] - '1';
  int tf = s[2] - 'a', tr = s[3] - '1';
  if (!on_board(ff, fr) || !on_board(tf, tr)) return m;
  m.from = (std::uint16_t)make_sq(ff, fr);
  m.to = (std::uint16_t)make_sq(tf, tr);
  if (s.size() >= 5) {
    switch (s[4]) {
      case 'n': m.promo = 1; break;
      case 'b': m.promo = 2; break;
      case 'r': m.promo = 3; break;
      case 'q': m.promo = 4; break;
      default: break;
    }
  }
  return m;
}

std::string Move::to_uci() const {
  std::string s = sq_name(from) + sq_name(to);
  if (promo == 1) s += 'n';
  else if (promo == 2) s += 'b';
  else if (promo == 3) s += 'r';
  else if (promo == 4) s += 'q';
  return s;
}

}  // namespace chessclaw
