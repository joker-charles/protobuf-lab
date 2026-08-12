#include <contracts>
#include <cstdio>
void handle_contract_violation(std::contracts::contract_violation const&) {}
int main() {
  int x = 42;
  contract_assert(x == 42);
  return 0;
}
