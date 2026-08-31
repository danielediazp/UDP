#include <cstddef>
#include <print>
#include <span>
#include <vector>

import udp;
import udp_representation;

int main(int argc, char *argv[]) {
  udp::package::Header header{
      .src_port = 12345, .dst_port = 80, .length = 0, .checksum = 0};
  std::println("{}", header);

  std::string data_str = "Daniel says hi!";
  auto bytes_data = std::as_bytes(std::span(data_str));

  std::vector<std::byte> data;
  data.reserve(data_str.size());
  data.insert(data.end(), bytes_data.begin(), bytes_data.end());

  udp::package::Package udp_package{.header = header, .data = data};
  std::println("{}", udp_package);

  return 0;
}
