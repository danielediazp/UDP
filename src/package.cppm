module;

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <netinet/ip.h>

export module package;

import utils;

export namespace package {
using port = std::uint16_t;
using datagram = std::vector<std::byte>;

#pragma pack(push, 1) // Force the compiler to make the struct strictly 8 bytes
struct Header {       // 64 bits
  port src_port{0};   // [0, 15]
  port dst_port{0};   // [16, 31]
  std::uint16_t length{0};   // [32, 47]
  std::uint16_t checksum{0}; // [48, 63]
};
#pragma pack(pop)

inline constexpr std::size_t header_fields_ct = 4;
inline constexpr std::size_t header_sz = sizeof(Header);

static_assert(header_sz == 8, "udp::package::Header got extra padding.");

// MTU limits are around 1500 bytes.
struct Package {
  Header header; // [0, 64]
  datagram data; // [64, N] Size should be around 1472 to prevent package
                 // getting drops by MTU limits.
};

inline constexpr std::uint16_t data_mx_sz = 1472;

auto net_header_swap(const Header &header) -> Header {
  return Header{
      .src_port = utils::net_short_swaps(header.src_port),
      .dst_port = utils::net_short_swaps(header.dst_port),
      .length = utils::net_short_swaps(header.length),
      .checksum = utils::net_short_swaps(header.checksum),
  };
}

struct UDPPseudoHeader {
  std::uint32_t src_addr{0};
  std::uint32_t dest_addr{0};
  std::uint8_t zero{0};
  std::uint8_t protocol{IPPROTO_UDP};
  std::uint16_t udp_length{0};
};

/**
 * @brief Serializes a Package into its wire-format byte representation.
 *
 * Converts each multi-byte header field to network (big-endian) byte
 * order before writing it out; the payload bytes are copied as is.
 *
 * @param pkg  The package to serialize.
 *
 * @return std::expected<std::vector<std::byte>, udp_error> the vector of bytes
 * first 8 byte entries represent the header and the rest the data.
 */
auto serialize(const Package &pkg) -> std::vector<std::byte> {

  auto data_sz = pkg.data.size();

  std::vector<std::byte> ser_pkg;
  ser_pkg.reserve(header_sz + data_sz);

  auto endian_rep_header = net_header_swap(pkg.header);
  auto header_bytes = std::as_bytes(std::span(&endian_rep_header, 1));
  ser_pkg.insert(ser_pkg.end(), header_bytes.begin(), header_bytes.end());

  auto data_bytes = std::as_bytes(std::span(pkg.data));
  ser_pkg.insert(ser_pkg.end(), data_bytes.begin(), data_bytes.end());

  return ser_pkg;
}

enum class deserialization_error { too_few_bytes };

/**
 * @brief Deserialize a package from wire-format representation into a human
 * readable format.
 *
 * Assume the first header_sz byte of the ser_pkg represent
 * the udp::package::Header and the rest represents the data.
 *
 * @param ser_pkg the package to deserialize. Must contain the header and some
 *                data.
 *
 * @return std::expected<package::Package, deserialization_error> The package in
 * human readable format.
 */
[[nodiscard]] auto deserialize(const std::vector<std::byte> &ser_pkg)
    -> std::expected<Package, deserialization_error> {
  if (ser_pkg.size() < header_sz + 1) {
    return std::unexpected(deserialization_error::too_few_bytes);
  }

  std::array<std::byte, header_sz> header_bytes{};
  std::ranges::copy_n(ser_pkg.begin(), header_sz, header_bytes.begin());

  auto wire_header = std::bit_cast<Header>(header_bytes);

  return Package{.header = net_header_swap(wire_header),
                 .data = datagram(ser_pkg.begin() + header_sz, ser_pkg.end())};
}

} // namespace package