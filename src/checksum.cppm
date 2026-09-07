module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

export module checksum;

export namespace checksum {

inline constexpr std::uint16_t max_uint16 =
    std::numeric_limits<std::uint16_t>::max(); // 0xFFFF
inline constexpr std::uint8_t byte_sz_as_bits = 8;

auto checksum_acc(std::span<const std::byte> sp) -> std::uint32_t {
  std::uint32_t totals{0};
  for (auto i{0uz}; i + 1 < sp.size(); i += 2) {
    auto hsb = static_cast<std::uint32_t>(sp[i]) << byte_sz_as_bits;
    auto lsb = static_cast<std::uint32_t>(sp[i + 1]);
    auto word = hsb | lsb;
    totals = (totals + word) % max_uint16;
  }

  if (sp.size() % 2) {
    auto word = static_cast<std::uint32_t>(sp[sp.size() - 1])
                << byte_sz_as_bits;
    totals = (totals + word) % max_uint16;
  }

  return totals;
}

/**
 * @brief Computes Checksum is the 16-bit one's complement of the one's
 *        complement sum of a pseudo header of information from the IP header,
 *        the UDP header, and the data,  padded  with zero octets  at the end
 *       (if necessary) to  make  a multiple of two octets.
 *
 * @param pkg bytes corresponding to the packages where fields are presented in
 *            network format.
 * @param pshdr the udo pseudo header byte representation where every field is
 *              network format.
 * @return std::uint16_t the checksum result as a 16 bit unsigned integer
 */
[[nodiscard]] auto compute_checksum(std::span<const std::byte> pkg,
                                    std::span<const std::byte> pshdr)
    -> std::uint16_t {
  auto sum_pshdr = checksum_acc(pshdr);
  auto sum_pkg = checksum_acc(pkg);
  auto sum = sum_pshdr + sum_pkg;

  while (sum >> 16) {
    sum = (sum & max_uint16) + (sum >> 16);
  }

  auto checksum = static_cast<std::uint16_t>(~sum);
  return checksum == 0 ? max_uint16 : checksum;
}
} // namespace checksum