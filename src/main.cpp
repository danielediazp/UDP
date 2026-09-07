
#include <cstddef>
#include <cstdlib>
#include <print>
#include <ranges>
#include <vector>

import udp;
import udp_representation;

int main() {
  udp::UDPSocket sender{600, "127.0.0.1", 500};
  udp::UDPSocket receiver{500, "127.0.0.1", 600};

  std::string data_str = "Daniel says hi! through this package";
  auto dt = data_str |
            std::views::transform([](char c) { return std::byte(c); }) |
            std::ranges::to<std::vector<std::byte>>();

  if (!sender.send(dt)) {
    std::println("Sender failed to send the package");
    return EXIT_FAILURE;
  }

  std::println("Successfully submitted the package");

  auto rc = receiver.recv();
  if (!rc) {
    std::println("Receiver failed to receive package");
    return EXIT_FAILURE;
  }

  std::println("got package: {}", *rc);

  return EXIT_SUCCESS;
}
