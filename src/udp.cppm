module;

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

export module udp;

export namespace udp {
using Port = std::uint16_t;
using Data = std::vector<std::byte>;

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

namespace package {

#pragma pack(push, 1) // Force the compiler to make the struct strictly 8 bytes
struct Header {       // 64 bits
  Port src_port{0};   // [0, 15]
  Port dst_port{0};   // [16, 31]
  std::uint16_t length{0};   // [32, 47]
  std::uint16_t checksum{0}; // [48, 63]
};
#pragma pack(pop)

inline constexpr std::size_t header_fields_ct = 4;
inline constexpr std::size_t header_sz = sizeof(Header);

// MTU limits are around 1500 bytes.
struct Package {
  Header header; // [0, 64]
  Data data; // [64, N] Size should be around 1472 to prevent package getting
             // drops by MTU limits.
};

inline constexpr std::uint16_t data_mx_sz = 1472;

enum class deserialization_error { too_few_bytes };

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

  static_assert(header_sz == 8, "udp::package::Header got extra padding. "
                                "Unable to compute serialization");
  auto data_sz = pkg.data.size();

  auto &header = pkg.header;
  auto endian_rep_header = Header{
      .src_port = net_short_swaps(header.src_port),
      .dst_port = net_short_swaps(header.dst_port),
      .length = net_short_swaps(header.length),
      .checksum = net_short_swaps(header.checksum),
  };
  std::vector<std::byte> ser_pkg;
  ser_pkg.reserve(header_sz + data_sz);

  auto header_bytes = std::as_bytes(std::span(&endian_rep_header, 1));
  ser_pkg.insert(ser_pkg.end(), header_bytes.begin(), header_bytes.end());

  auto data_bytes = std::as_bytes(std::span(pkg.data));
  ser_pkg.insert(ser_pkg.end(), data_bytes.begin(), data_bytes.end());

  return ser_pkg;
}

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

  const auto get_host_friendly_endian = [](const Header &h) {
    return Header{.src_port = net_short_swaps(h.src_port),
                  .dst_port = net_short_swaps(h.dst_port),
                  .length = net_short_swaps(h.length),
                  .checksum = net_short_swaps(h.checksum)};
  };

  return Package{.header = get_host_friendly_endian(wire_header),
                 .data = Data(ser_pkg.begin() + header_sz, ser_pkg.end())};
}

// TODO: Implement. The checksum is the 16 bits one complements sum of the IP
// header + udp package. If the result is 0, we flip all the bits to be 1s.
auto compute_check_sum(Package &pkg) -> std::uint16_t { return 0; }

} // namespace package

class udp_socket_creation_error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class udp_error { mtu_limit_exceeded, send_failed };

class UDP {
public:
  UDP() {
    socket_fd_ = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (socket_fd_ < 0) {
      throw udp_socket_creation_error(
          "Unable to create socket for UDP instance");
    }
  }

  ~UDP() {
    if (socket_fd_ == -1) {
      return;
    }

    close(socket_fd_);
  }

  int receive(const package::Package &package, Port src_port, Port dst_port) {
    return 0;
  }

  auto send(Data dt, Port src_port, Port dst_port, const std::string &dst_addr)
      -> std::expected<void, udp_error> {

    auto data_sz = dt.size();

    if (data_sz > package::data_mx_sz) {
      return std::unexpected(udp_error::mtu_limit_exceeded);
    }

    auto hd = package::Header{
        .src_port = src_port,
        .dst_port = dst_port,
        .length = static_cast<std::uint16_t>(data_sz),
    };
    auto pkg = package::Package{.header = hd, .data = std::move(dt)};
    pkg.header.checksum = package::compute_check_sum(pkg);

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = pkg.header.dst_port;
    dest_addr.sin_addr.s_addr = ::inet_addr(dst_addr.data());

    auto pkg_bytes = package::serialize(pkg);

    ssize_t bytes_sent =
        sendto(socket_fd_, pkg_bytes.data(), pkg_bytes.size(), 0,
               reinterpret_cast<sockaddr *>(&dest_addr), sizeof(dst_addr));

    if (bytes_sent < 0) {
      return std::unexpected(udp_error::send_failed);
    }

    return {};
  }

  // Delete to prevent double socket closure
  UDP(const UDP &other) = delete;
  UDP &operator=(const UDP &other) = delete;

  UDP(UDP &&other) noexcept : socket_fd_(other.socket_fd_) {
    other.socket_fd_ = -1;
  }

  UDP &operator=(UDP &&other) noexcept {
    if (this != &other) {
      socket_fd_ = other.socket_fd_;
      other.socket_fd_ = -1;
    }

    return *this;
  }

private:
  int socket_fd_ = -1;
};
} // namespace udp