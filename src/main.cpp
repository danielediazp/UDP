#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <print>
#include <span>
#include <vector>

import udp;
import udp_representation;

int main(int argc, char *argv[]) {
  udp::package::Header header{
      .src_port = 97, .dst_port = 99, .length = 65, .checksum = 66};
  std::println("{}", header);

  std::string data_str = "Daniel says hi!";
  auto bytes_data = std::as_bytes(std::span(data_str));

  std::vector<std::byte> data;
  data.reserve(data_str.size());
  data.insert(data.end(), bytes_data.begin(), bytes_data.end());

  udp::package::Package udp_package{.header = header, .data = data};
  std::println("{}", udp_package);

  auto ser_pkg = udp::package::serialize(udp_package);
  if (!ser_pkg.has_value()) {
    std::println(stderr, "Unable to serialize udp package");
    return 1;
  }

  std::string ser_pkg_str;
  ser_pkg_str.reserve(ser_pkg->size());
  std::ranges::transform(*ser_pkg, std::back_inserter(ser_pkg_str),
                         [](auto b) { return static_cast<char>(b); });
  std::println("serialize package: {}", ser_pkg_str);

  auto pkg = udp::package::deserialize(*ser_pkg);
  if (!pkg.has_value()) {
    std::println(stderr, "Unable to deserialize udp package");
    return 1;
  }

  std::println("deserialize package: {}", *pkg);
  return 0;
}
