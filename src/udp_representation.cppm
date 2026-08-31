module;

#include <algorithm>
#include <format>
#include <iterator>
#include <string>

export module udp_representation;

import udp;

export template <> struct std::formatter<udp::package::Header> {
  constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

  auto format(const udp::package::Header &h, std::format_context &ctx) const {
    return std::format_to(
        ctx.out(),
        "Header(src_port: {}, dst_port: {}, length: {}, checksum: {})",
        h.src_port, h.dst_port, h.length, h.checksum);
  }
};

export template <> struct std::formatter<udp::package::Package> {
  constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }

  auto format(const udp::package::Package &p, std::format_context &ctx) const {
    std::string data_as_str;
    data_as_str.reserve(p.data.size());
    std::ranges::transform(p.data, std::back_inserter(data_as_str),
                           [](std::byte b) { return static_cast<char>(b); });

    return std::format_to(ctx.out(), "Package(header: {}, data: \"{}\")",
                          p.header, data_as_str);
  }
};