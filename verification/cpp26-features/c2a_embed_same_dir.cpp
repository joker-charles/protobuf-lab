#include <print>
constexpr unsigned char data[] = {
#embed "data.bin"
};
static_assert(__cpp_pp_embed == 202502L);
int main() { std::println("{} {}", (int)sizeof(data), __cpp_pp_embed); }
