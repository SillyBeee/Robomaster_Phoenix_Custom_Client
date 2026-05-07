#include "raw300_decoder/decoder_utils.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace hrvtx::standalone
{

std::string hex_preview(const std::vector<uint8_t> &data, size_t max_len)
{
    const auto n = std::min(max_len, data.size());
    if (n == 0)
    {
        return "(empty)";
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i)
    {
        if (i > 0)
        {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

std::string annex_b_hint(const std::vector<uint8_t> &data)
{
    if (data.empty())
    {
        return "payload empty";
    }

    std::vector<std::string> hints;
    if (data.size() >= 3 && data[0] == 0x00 && data[1] == 0x00 &&
        data[2] == 0x01)
    {
        hints.emplace_back("start code 00 00 01");
    }
    else if (data.size() >= 4 && data[0] == 0x00 && data[1] == 0x00 &&
             data[2] == 0x00 && data[3] == 0x01)
    {
        hints.emplace_back("start code 00 00 00 01");
    }
    else
    {
        hints.emplace_back("payload does not start with Annex-B start code");
    }

    size_t n3 = 0;
    for (size_t i = 0; i + 3 <= data.size(); ++i)
    {
        if (data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01)
        {
            ++n3;
        }
    }
    if (n3 > 0)
    {
        hints.emplace_back("contains 00 00 01 ~= " + std::to_string(n3));
    }

    std::ostringstream oss;
    for (size_t i = 0; i < hints.size(); ++i)
    {
        if (i > 0)
        {
            oss << "; ";
        }
        oss << hints[i];
    }
    return oss.str();
}

std::vector<int> nal_unit_types(const std::vector<uint8_t> &data)
{
    std::vector<int> out;
    size_t i = 0;
    while (i < data.size())
    {
        if (i + 3 <= data.size() && data[i] == 0 && data[i + 1] == 0 &&
            data[i + 2] == 1)
        {
            if (i + 3 < data.size())
            {
                out.push_back(static_cast<int>(data[i + 3] & 0x1F));
            }
            i += 3;
            continue;
        }
        if (i + 4 <= data.size() && data[i] == 0 && data[i + 1] == 0 &&
            data[i + 2] == 0 && data[i + 3] == 1)
        {
            if (i + 4 < data.size())
            {
                out.push_back(static_cast<int>(data[i + 4] & 0x1F));
            }
            i += 4;
            continue;
        }
        ++i;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

bool contains_type(const std::vector<int> &types, int t)
{
    return std::find(types.begin(), types.end(), t) != types.end();
}

int nal_type_from_annexb_nal(const std::vector<uint8_t> &nal)
{
    if (nal.size() >= 5 && nal[0] == 0 && nal[1] == 0 && nal[2] == 0 &&
        nal[3] == 1)
    {
        return static_cast<int>(nal[4] & 0x1F);
    }
    if (nal.size() >= 4 && nal[0] == 0 && nal[1] == 0 && nal[2] == 1)
    {
        return static_cast<int>(nal[3] & 0x1F);
    }
    return -1;
}

} // namespace hrvtx::standalone
