#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hrvtx::standalone
{

std::string hex_preview(const std::vector<uint8_t> &data, size_t max_len);

std::string annex_b_hint(const std::vector<uint8_t> &data);

std::vector<int> nal_unit_types(const std::vector<uint8_t> &data);

bool contains_type(const std::vector<int> &types, int t);

int nal_type_from_annexb_nal(const std::vector<uint8_t> &nal);

} // namespace hrvtx::standalone
