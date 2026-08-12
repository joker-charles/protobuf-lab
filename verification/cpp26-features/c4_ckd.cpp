#include <print>
#include <stdckdint.h>
int main() {
  int a = 2147483647, b = 1, r;
  bool ovf = ckd_add(&r, a, b);
  std::println("ovf={} r={}", ovf, r);
  unsigned u = 3;
  bool ovf2 = ckd_sub(&u, 2u, 5u);
  std::println("ovf2={} u={}", ovf2, u);
}
