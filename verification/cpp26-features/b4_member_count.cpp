#include <cstddef>
#include <meta>
struct S { int a; float b; double c; };
template <typename T>
constexpr std::size_t count =
    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unprivileged()).size();
static_assert(count<S> == 3);
int main() {}
