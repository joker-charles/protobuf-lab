#include <print>
constexpr unsigned char data[] = {
#embed "data.bin"
};
int main() { std::println("{}", (int)sizeof(data)); }
