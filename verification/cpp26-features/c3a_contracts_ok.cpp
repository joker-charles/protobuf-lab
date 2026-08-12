#include <contracts>
#include <print>
void handle_contract_violation(std::contracts::contract_violation const& v) {
  std::println("violation: {}", v.comment());
}
int main() {
  int x = 42;
  contract_assert(x == 42);
  std::println("ok");
}
