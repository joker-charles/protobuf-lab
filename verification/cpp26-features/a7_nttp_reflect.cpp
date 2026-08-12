#include <meta>
template <int N>
consteval std::meta::info f() { return ^^N; }
int main() { static_assert(f<42>() != std::meta::info{}); }
