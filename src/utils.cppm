module;

#include <bit>
#include <cstdint>

export module utils;

export namespace utils {
/**
 * @brief Swaps the bytes from little endian to big endian and vice versa.
 *
 * @param v the value top swap in little endian format.
 *
 * @return std::uint16_t the value swap to big endian format.
 */
[[nodiscard]] constexpr auto net_short_swaps(std::uint16_t v) noexcept
    -> std::uint16_t {
  if constexpr (std::endian::native == std::endian::little) {
    return std::byteswap(v);
  }

  return v;
}

} // namespace utils