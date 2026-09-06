module;

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>

export module udp;
import package;
import checksum;

export namespace udp {
using port = std::uint16_t;
using datagram = std::vector<std::byte>;

inline constexpr std::size_t iphdr_sz = sizeof(struct ip);

class udp_socket_creation_error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

enum class send_error {
  mtu_limit_exceeded,
  send_failed,
};
enum class receive_error {
  socket_crashed,
  malformed_package,
  corrupted_package,
};

class UDPSocket {
public:
  UDPSocket(port dst_port, std::string dst_addr, std::optional<port> src_port)
      : src_port_{src_port}, dst_port_(dst_port), dst_addr_(dst_addr) {
    socket_fd_ = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (socket_fd_ < 0) {
      throw udp_socket_creation_error(
          "Unable to create socket for UDPSocket instance");
    }
  }

  ~UDPSocket() {
    if (socket_fd_ == -1) {
      return;
    }

    close(socket_fd_);
  }

  [[nodiscard]] std::expected<package::Package, receive_error> recv() {
    std::vector<std::byte> buff;
    buff.resize(1501);
    while (true) {
      ssize_t bytes =
          ::recvfrom(socket_fd_, buff.data(), buff.size(), 0, nullptr, nullptr);

      if (bytes < 0) {
        // Receive a signal mid call, retry
        if (errno == EINTR) {
          continue;
        }

        return std::unexpected(receive_error::socket_crashed);
      }

      if (bytes < iphdr_sz) {
        continue;
      }

      auto it_buff_begin = buff.begin() + iphdr_sz;
      auto it_buff_end = it_buff_begin + (bytes - iphdr_sz);
      std::vector<std::byte> upd_dt{it_buff_begin, it_buff_end};
      auto pkg = package::deserialize(upd_dt);
      if (!pkg.has_value()) {
        return std::unexpected(receive_error::malformed_package);
      }

      // We don't own this package. The kernel returns all matching data for the
      // protocol for raw sockets since they are not bind to a port
      const auto &pkg_hd = pkg->header;
      if (pkg_hd.dst_port != src_port_) {
        continue;
      }

      if (pkg_hd.checksum != 0 &&
          pkg_hd.checksum != checksum::compute_checksum(*pkg)) {
        return std::unexpected(receive_error::corrupted_package);
      }

      return std::move(*pkg);
    }
  }

  [[nodiscard]] auto send(datagram dt) -> std::expected<void, send_error> {

    auto data_sz = dt.size();

    if (data_sz > package::data_mx_sz) {
      return std::unexpected(send_error::mtu_limit_exceeded);
    }

    auto hd = package::Header{
        .src_port = src_port_.value_or(0),
        .dst_port = dst_port_,
        .length = static_cast<std::uint16_t>(data_sz + package::header_sz),
    };
    auto pkg = package::Package{.header = hd, .data = std::move(dt)};
    pkg.header.checksum = checksum::compute_checksum(pkg);

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = pkg.header.dst_port;
    dest_addr.sin_addr.s_addr = ::inet_addr(dst_addr_.data());

    auto pkg_bytes = package::serialize(pkg);

    ssize_t bytes_sent =
        ::sendto(socket_fd_, pkg_bytes.data(), pkg_bytes.size(), 0,
                 reinterpret_cast<sockaddr *>(&dest_addr), sizeof(dest_addr));

    if (bytes_sent < 0) {
      return std::unexpected(send_error::send_failed);
    }

    return {};
  }

  // Delete to prevent double socket closure
  UDPSocket(const UDPSocket &other) = delete;
  UDPSocket &operator=(const UDPSocket &other) = delete;

  UDPSocket(UDPSocket &&other) noexcept : socket_fd_(other.socket_fd_) {
    other.socket_fd_ = -1;
  }

  UDPSocket &operator=(UDPSocket &&other) noexcept {
    if (this != &other) {
      socket_fd_ = other.socket_fd_;
      other.socket_fd_ = -1;
    }

    return *this;
  }

private:
  int socket_fd_ = -1;
  std::optional<port> src_port_;
  port dst_port_;
  std::string dst_addr_;
};
} // namespace udp