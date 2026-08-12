#include <meta>
struct S { int a; };
consteval auto n() { return std::meta::name_of(^^S); }
int main() {}
