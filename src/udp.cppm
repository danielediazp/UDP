module;

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module udp;
import package;
import checksum;
import utils;
import udp_representation;

export namespace udp {
using port = std::uint16_t;
using datagram = std::vector<std::byte>;

inline constexpr ssize_t iphdr_sz = sizeof(struct ip);

class udp_socket_creation_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct NetConnectionIps {
  std::uint32_t ip_src;
  std::uint32_t ip_dst;
};

enum class send_error {
  mtu_limit_exceeded,
  send_failed,
  unable_to_compute_ips,
  unable_to_parse_dst_addr,
  unexpected_err,
};
enum class receive_error {
  socket_crashed,
  malformed_package,
  corrupted_package,
  timeout,
};

class UDPSocket {
 public:
  UDPSocket(port dst_port, std::string dst_addr,
            std::optional<port> src_port = std::nullopt,
            std::uint64_t read_timeout = 2)
      : src_port_{src_port},
        dst_port_(dst_port),
        dst_addr_(dst_addr),
        read_timeout_(read_timeout) {
    socket_fd_ = ::socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    if (socket_fd_ < 0) {
      throw udp_socket_creation_error(
          "Unable to create socket for UDPSocket instance");
    }

    timeval tv{
        .tv_sec = static_cast<time_t>(read_timeout_),
        .tv_usec = 0,
    };

    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) <
        0) {
      ::close(socket_fd_);
      throw udp_socket_creation_error("Unable to set the receive timeout");
    }
  }

  ~UDPSocket() {
    if (socket_fd_ == -1) {
      return;
    }

    ::close(socket_fd_);
  }

  [[nodiscard]] auto recv() -> std::expected<package::Package, receive_error> {
    constexpr std::size_t max_package_size =
        std::numeric_limits<std::uint16_t>::max() + 1;
    std::array<std::byte, max_package_size> buff{};

    while (true) {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr = {.s_addr = INADDR_ANY};
      socklen_t addrlen = sizeof(addr);
      ssize_t bytes = ::recvfrom(socket_fd_, buff.data(), buff.size(), 0,
                                 reinterpret_cast<sockaddr*>(&addr), &addrlen);

      if (bytes < 0) {
        // Receive a signal mid call, retry
        if (errno == EINTR) {
          continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          return std::unexpected(receive_error::timeout);
        }

        return std::unexpected(receive_error::socket_crashed);
      }

      if (bytes < iphdr_sz) {
        continue;
      }

      std::array<std::byte, iphdr_sz> ip_bytes{};
      std::ranges::copy_n(buff.begin(), iphdr_sz, ip_bytes.begin());
      auto iphdr = std::bit_cast<struct ip>(ip_bytes);

      const ssize_t ihl = iphdr.ip_hl * 4;
      if (ihl < iphdr_sz || bytes < ihl) {
        continue;
      }

      auto it_buff_begin = buff.begin() + ihl;
      auto it_buff_end = it_buff_begin + (bytes - ihl);
      std::vector<std::byte> pkg_as_bytes{it_buff_begin, it_buff_end};
      auto pkg = package::deserialize(pkg_as_bytes);
      if (!pkg.has_value()) {
        continue;
      }

      // We don't own this package. The kernel returns all matching data for the
      // protocol for raw sockets since they are not bind to a port
      const auto& pkg_hd = pkg->header;
      if (src_port_.has_value() && pkg_hd.dst_port != src_port_) {
        continue;
      }

      if (pkg_as_bytes.size() != pkg_hd.length) {
        return std::unexpected(receive_error::corrupted_package);
      }

      auto pshdr_as_bytes = package::get_udp_pshdr_as_bytes(
          iphdr.ip_src.s_addr, iphdr.ip_dst.s_addr,
          utils::net_short_swaps(pkg_hd.length));

      if (pkg_hd.checksum != 0 &&
          checksum::compute_checksum(pkg_as_bytes, pshdr_as_bytes) !=
              checksum::max_uint16) {
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

    auto pkg_sz = static_cast<std::uint16_t>(data_sz + package::header_sz);
    auto hd = package::Header{
        .src_port = src_port_.value_or(0),
        .dst_port = dst_port_,
        .length = pkg_sz,
    };
    auto pkg = package::Package{.header = hd, .data = std::move(dt)};

    auto ips = get_connection_ips();
    if (!ips.has_value()) {
      return std::unexpected(send_error::unable_to_compute_ips);
    }

    auto pshdr_as_bytes = package::get_udp_pshdr_as_bytes(
        ips->ip_src, ips->ip_dst, utils::net_short_swaps(pkg_sz));
    auto pkg_as_bytes = package::serialize(pkg);
    auto checksum =
        checksum::compute_checksum(std::span(pkg_as_bytes), pshdr_as_bytes);

    if (!package::insert_checksum_in_pkg(pkg_as_bytes, checksum).has_value()) {
      return std::unexpected(send_error::unexpected_err);
    }

    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = utils::net_short_swaps(pkg.header.dst_port);
    if (::inet_pton(AF_INET, dst_addr_.data(), &dest_addr.sin_addr) <= 0) {
      return std::unexpected(send_error::unable_to_parse_dst_addr);
    }

    ssize_t bytes_sent =
        ::sendto(socket_fd_, pkg_as_bytes.data(), pkg_as_bytes.size(), 0,
                 reinterpret_cast<sockaddr*>(&dest_addr), sizeof(dest_addr));

    if (bytes_sent < 0) {
      return std::unexpected(send_error::send_failed);
    }

    return {};
  }

  // Delete to prevent double socket closure
  UDPSocket(const UDPSocket& other) = delete;
  UDPSocket& operator=(const UDPSocket& other) = delete;

  UDPSocket(UDPSocket&& other) noexcept
      : socket_fd_(other.socket_fd_),
        src_port_(other.src_port_),
        dst_port_(other.dst_port_),
        dst_addr_(std::move(other.dst_addr_)),
        read_timeout_(other.read_timeout_) {
    other.socket_fd_ = -1;
  }

  UDPSocket& operator=(UDPSocket&& other) noexcept {
    if (this != &other) {
      // Release what we own
      if (socket_fd_ != -1) {
        ::close(socket_fd_);
      }

      socket_fd_ = std::exchange(other.socket_fd_, -1);
      src_port_ = other.src_port_;
      dst_port_ = other.dst_port_;
      dst_addr_ = std::move(other.dst_addr_);
      read_timeout_ = other.read_timeout_;
    }

    return *this;
  }

 private:
  int socket_fd_ = -1;
  std::optional<port> src_port_;
  port dst_port_;
  std::string dst_addr_;
  std::uint64_t read_timeout_;

  /**
   * @brief Resolves the local and remote IPv4 addresses for this socket's
   *        configured destination.
   *
   * It most be computed for every checksum validation since Ips are dynamic in
   * multi-homed hosts and different destinations resolve to different local
   * addresses depending on the connection (Ethernet - Wifi, Wifi - VPN, ..)
   *
   * @note Both addresses are returned in network byte order, ready to be
   *       copied into a UDP pseudo header without further conversion.
   *
   * @return The local and remote addresses on success; `std::nullopt` if the
   *         destination string is unparseable, no route exists to it, or the
   *         socket state cannot be queried.
   */
  [[nodiscard]] auto get_connection_ips() -> std::optional<NetConnectionIps> {
    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = utils::net_short_swaps(dst_port_);

    if (::inet_pton(AF_INET, dst_addr_.data(), &target_addr.sin_addr) <= 0) {
      return std::nullopt;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&target_addr),
                  sizeof(target_addr)) < 0) {
      return std::nullopt;
    }

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    socklen_t local_addr_sz = sizeof(local_addr);

    if (::getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&local_addr),
                      &local_addr_sz) < 0) {
      return std::nullopt;
    }

    return NetConnectionIps{
        .ip_src = local_addr.sin_addr.s_addr,
        .ip_dst = target_addr.sin_addr.s_addr,
    };
  }
};
}  // namespace udp