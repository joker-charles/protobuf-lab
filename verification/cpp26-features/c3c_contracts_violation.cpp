#include <contracts>
#include <cstdio>
void handle_contract_violation(std::contracts::contract_violation const& v) {
  std::fprintf(stderr, "handler-called comment=%s\n", v.comment());
}
int main() {
  int x = 41;
  contract_assert(x == 42);
  std::puts("after");
}
