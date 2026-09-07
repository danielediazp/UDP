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

inline constexpr std::size_t header_sz = sizeof(Header);

inline constexpr std::size_t expected_hd_sz = 8;
static_assert(header_sz == expected_hd_sz,
              "package::Header got extra padding.");

// MTU limits are around 1500 bytes.
// IP header (handle by the kernel) can be 20-60 bytes
// UDP header is 8 bytes
// Data is the rest keeping it bellow 1500 to ensure delivery
struct Package {
  Header header; // [0, 64]
  datagram data; // [64, N] recommended size of a maximum of 1432 bytes, so it
                 // doesn't get drop by ethernet cable limits respecting the MTU
};

inline constexpr std::uint16_t data_mx_sz = 1432;

auto net_header_swap(const Header &header) -> Header {
  return Header{
      .src_port = utils::net_short_swaps(header.src_port),
      .dst_port = utils::net_short_swaps(header.dst_port),
      .length = utils::net_short_swaps(header.length),
      .checksum = utils::net_short_swaps(header.checksum),
  };
}

#pragma pack(push, 1)
struct PseudoHeader { // 96 bits
  std::uint32_t src_addr{0};
  std::uint32_t dest_addr{0};
  std::uint8_t zero{0};
  std::uint8_t protocol{IPPROTO_UDP};
  std::uint16_t udp_length{0};
};
#pragma pack(pop)

inline constexpr std::size_t pseudoheader_sz = sizeof(PseudoHeader);

inline constexpr std::size_t expected_pshdr_sz = 12;
static_assert(pseudoheader_sz == expected_pshdr_sz,
              "package::PseudoHeader got unexpected padding.");

/**
 * @brief Get the udp pshdr as bytes object.
 *
 * @param src_addr source address in network format.
 * @param dest_addr destination address in network format.
 * @param udp_length package length in network format.
 * @return std::array<std::byte, 12> the pseudo header representation as bytesin
 * network format.
 */
auto get_udp_pshdr_as_bytes(std::uint32_t src_addr, std::uint32_t dest_addr,
                            std::uint16_t udp_length)
    -> std::array<std::byte, pseudoheader_sz> {
  return std::bit_cast<std::array<std::byte, pseudoheader_sz>>(PseudoHeader{
      .src_addr = src_addr,
      .dest_addr = dest_addr,
      .udp_length = udp_length,
  });
}

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

  ser_pkg.insert(
      ser_pkg.end(), pkg.data.begin(),
      pkg.data.end()); // Copy as is since it is already in byte format

  return ser_pkg;
}

enum class wire_format_error { too_few_bytes };

auto insert_checksum_in_pkg(std::vector<std::byte> &pkg, std::uint16_t checksum)
    -> std::expected<void, wire_format_error> {

  if (pkg.size() < header_sz) {
    return std::unexpected(wire_format_error::too_few_bytes);
  }
  constexpr std::size_t checksum_hsb = 6;
  constexpr std::size_t checksum_lsb = 7;
  pkg[checksum_hsb] = std::byte(checksum >> 8);
  pkg[checksum_lsb] = std::byte(checksum & 0xFF);

  return {};
}

/**
 * @brief Deserialize a package from wire-format representation into a human
 * readable format.
 *
 * Assume the first header_sz byte of the ser_pkg represent
 * the package::Header and the rest represents the data.
 *
 * @param ser_pkg the package to deserialize. Must consist of a minimum of 8
 * bytes representing the header.
 *
 * @return std::expected<package::Package, wire_format_error> The package in
 * human readable format.
 */
auto deserialize(const std::vector<std::byte> &ser_pkg)
    -> std::expected<Package, wire_format_error> {
  if (ser_pkg.size() < header_sz) {
    return std::unexpected(wire_format_error::too_few_bytes);
  }

  std::array<std::byte, header_sz> header_bytes{};
  std::ranges::copy_n(ser_pkg.begin(), header_sz, header_bytes.begin());

  auto wire_header = std::bit_cast<Header>(header_bytes);

  return Package{
      .header = net_header_swap(wire_header),
      .data = datagram(ser_pkg.begin() + header_sz,
                       ser_pkg.end()) // Can be copied as is because endian
                                      // format only matter for multi byte words
  };
}

} // namespace package