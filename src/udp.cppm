module;

#include <cstddef>
#include <cstdint>
#include <expected>
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

namespace package {
struct Header {           // 64 bits
  Port src_port;          // [0, 15]
  Port dst_port;          // [16, 31]
  std::uint16_t length;   // [32, 47]
  std::uint16_t checksum; // [48, 63]
};

// MTU limits are around 1500 bytes.
struct Package {
  Header header; // [0, 64]
  Data data;     // [64, N]
};
} // namespace package

enum class udp_error { serialization_error, deserialization_error };

auto serialize(const package::Package &pck)
    -> std::expected<std::vector<std::byte>, udp_error> {
  return {};
}

auto deserialize(const std::vector<std::byte> &ser_pck)
    -> std::expected<package::Package, udp_error> {
  return {};
}

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