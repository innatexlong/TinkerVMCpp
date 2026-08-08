#include <string_view>
#include <functional>
#include <cstdint>
#include <array>


namespace std {
    template<size_t N>
    struct hash<std::array<uint8_t, N>> {
        size_t operator()(const std::array<uint8_t, N>& arr) const noexcept {
            return hash<std::string_view>{}({reinterpret_cast<const char*>(arr.data()), N});
        }
    };
}