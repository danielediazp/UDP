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

enum class udp_error { mtu_limit_exceeded, malformed_pkg };

namespace package {
struct Header {           // 64 bits
  Port src_port;          // [0, 15]
  Port dst_port;          // [16, 31]
  std::uint16_t length;   // [32, 47]
  std::uint16_t checksum; // [48, 63]
};

inline constexpr std::size_t header_fields_ct = 4;
inline constexpr std::size_t header_sz = sizeof(Header);

// MTU limits are around 1500 bytes.
struct Package {
  Header header; // [0, 64]
  Data data; // [64, N] Size should be around 1472 to prevent package getting
             // drops by MTU limits.
};

inline constexpr std::uint16_t data_mx_sz = 1472;

/**
 * @brief Serializes a Package into its wire-format byte representation.
 *
 * Converts each multi-byte header field to network (big-endian) byte
 * order before writing it out; the payload bytes are copied as is.
 *
 * @param pkg  The package to serialize. `pkg.data` must not exceed
 *             `data_mx_sz` bytes.
 *
 * @return std::expected<std::vector<std::byte>, udp_error> the vector of bytes
 * first 8 byte entries represent the header and the rest the data.
 */
[[nodiscard]] auto serialize(const Package &pkg)
    -> std::expected<std::vector<std::byte>, udp_error> {

  static_assert(header_sz == 8, "udp::package::Header got extra padding. "
                                "Unable to compute serialization");
  auto data_sz = pkg.data.size();
  if (data_sz > data_mx_sz) {
    return std::unexpected(udp_error::mtu_limit_exceeded);
  }

  auto &header = pkg.header;
  auto endian_rep_header = Header{
      .src_port = htons(header.src_port),
      .dst_port = htons(header.dst_port),
      .length = htons(header.length),
      .checksum = htons(header.checksum),
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
 * @return std::expected<package::Package, udp_error> The package in human
 * readable format.
 */
[[nodiscard]] auto deserialize(const std::vector<std::byte> &ser_pkg)
    -> std::expected<package::Package, udp_error> {

  if (ser_pkg.size() < header_sz + 1) {
    return std::unexpected(udp_error::malformed_pkg);
  }

  std::array<std::byte, header_sz> header_bytes{};
  std::ranges::copy_n(ser_pkg.begin(), header_sz, header_bytes.begin());

  auto wire_header = std::bit_cast<Header>(header_bytes);

  const auto get_host_friendly_endian = [](const Header &h) {
    return Header{.src_port = ntohs(h.src_port),
                  .dst_port = ntohs(h.dst_port),
                  .length = ntohs(h.length),
                  .checksum = ntohs(h.checksum)};
  };

  return Package{.header = get_host_friendly_endian(wire_header),
                 .data = Data(ser_pkg.begin() + header_sz, ser_pkg.end())};
}

} // namespace package

class udp_socket_creation_error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

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

  int send(const Data &dt, Port src_port, Port dst_port,
           const std::string &dst_addr) {
    return 0;
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