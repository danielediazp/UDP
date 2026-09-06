module;

#include <cstdint>

export module checksum;

import package;

export namespace checksum {
auto compute_checksum(const package::Package &pkg) -> std::uint16_t {
  return 0;
}
} // namespace checksum