#include "raw300_decoder/annexb_reassembler.hpp"

#include <algorithm>

#include "raw300_decoder/decoder_utils.hpp"

namespace hrvtx::standalone
{

AnnexBReassembler::AnnexBReassembler(size_t max_buffer_bytes)
    : max_buffer_bytes_(max_buffer_bytes)
{
}

void AnnexBReassembler::push_chunk(const std::vector<uint8_t> &chunk)
{
    if (chunk.empty())
    {
        return;
    }
    const auto old_size = byte_buffer_.size();
    byte_buffer_.resize(old_size + chunk.size());
    std::copy(chunk.begin(), chunk.end(), byte_buffer_.begin() + old_size);
    parse_buffer();
    resync_if_oversized();
}

std::vector<std::vector<uint8_t>> AnnexBReassembler::take_ready_access_units()
{
    std::vector<std::vector<uint8_t>> out;
    out.reserve(ready_aus_.size());
    while (!ready_aus_.empty())
    {
        out.push_back(std::move(ready_aus_.front()));
        ready_aus_.pop_front();
    }
    return out;
}

void AnnexBReassembler::flush()
{
    emit_current_au();
    byte_buffer_.clear();
}

const ReassemblerStats &AnnexBReassembler::stats() const
{
    return stats_;
}

void AnnexBReassembler::parse_buffer()
{
    if (byte_buffer_.size() < 4)
    {
        return;
    }

    size_t first_len = 0;
    const size_t first_start = find_start_code(byte_buffer_, 0, first_len);
    if (first_start == std::string::npos)
    {
        if (byte_buffer_.size() > max_buffer_bytes_ / 2)
        {
            byte_buffer_.clear();
            ++stats_.resync_count;
        }
        return;
    }

    if (first_start > 0)
    {
        byte_buffer_.erase(byte_buffer_.begin(),
                           byte_buffer_.begin() +
                               static_cast<std::ptrdiff_t>(first_start));
    }

    size_t current_start = 0;
    while (true)
    {
        size_t current_len = 0;
        current_start = find_start_code(byte_buffer_, current_start, current_len);
        if (current_start == std::string::npos)
        {
            break;
        }
        size_t next_len = 0;
        const size_t next_start =
            find_start_code(byte_buffer_, current_start + current_len, next_len);
        if (next_start == std::string::npos)
        {
            break;
        }
        std::vector<uint8_t> nal(
            byte_buffer_.begin() + static_cast<std::ptrdiff_t>(current_start),
            byte_buffer_.begin() + static_cast<std::ptrdiff_t>(next_start));
        process_nal(nal);
        current_start = next_start;
    }

    if (current_start > 0 && current_start < byte_buffer_.size())
    {
        byte_buffer_.erase(byte_buffer_.begin(),
                           byte_buffer_.begin() +
                               static_cast<std::ptrdiff_t>(current_start));
    }
}

void AnnexBReassembler::process_nal(const std::vector<uint8_t> &nal)
{
    const int nal_type = nal_type_from_annexb_nal(nal);
    ++stats_.nal_extracted;

    const bool is_vcl = nal_type == 1 || nal_type == 5;
    const bool is_boundary_non_vcl =
        nal_type == 9 || nal_type == 7 || nal_type == 8;

    if (current_has_vcl_ && (is_vcl || is_boundary_non_vcl))
    {
        emit_current_au();
    }

    if (nal_type == 7)
    {
        last_sps_ = nal;
    }
    else if (nal_type == 8)
    {
        last_pps_ = nal;
    }

    current_au_nals_.push_back(nal);
    if (is_vcl)
    {
        current_has_vcl_ = true;
    }
}

void AnnexBReassembler::emit_current_au()
{
    if (current_au_nals_.empty())
    {
        current_has_vcl_ = false;
        return;
    }
    if (!current_has_vcl_)
    {
        ++stats_.au_dropped;
        current_au_nals_.clear();
        return;
    }

    bool has_sps = false;
    bool has_pps = false;
    bool has_idr = false;
    for (const auto &nal : current_au_nals_)
    {
        const int t = nal_type_from_annexb_nal(nal);
        has_sps = has_sps || (t == 7);
        has_pps = has_pps || (t == 8);
        has_idr = has_idr || (t == 5);
    }

    std::vector<uint8_t> au_bytes;
    auto append_nal = [&au_bytes](const std::vector<uint8_t> &nal)
    {
        au_bytes.insert(au_bytes.end(), nal.begin(), nal.end());
    };

    if (has_idr)
    {
        if (!has_sps && !last_sps_.empty())
        {
            append_nal(last_sps_);
        }
        if (!has_pps && !last_pps_.empty())
        {
            append_nal(last_pps_);
        }
    }
    for (const auto &nal : current_au_nals_)
    {
        append_nal(nal);
    }

    ready_aus_.push_back(std::move(au_bytes));
    ++stats_.au_emitted;
    current_au_nals_.clear();
    current_has_vcl_ = false;
}

void AnnexBReassembler::resync_if_oversized()
{
    if (byte_buffer_.size() <= max_buffer_bytes_)
    {
        return;
    }
    size_t code_len = 0;
    const size_t start = find_start_code(
        byte_buffer_,
        byte_buffer_.size() -
            std::min(byte_buffer_.size(), max_buffer_bytes_ / 2),
        code_len);
    if (start == std::string::npos)
    {
        byte_buffer_.clear();
    }
    else
    {
        byte_buffer_.erase(byte_buffer_.begin(),
                           byte_buffer_.begin() +
                               static_cast<std::ptrdiff_t>(start));
    }
    current_au_nals_.clear();
    current_has_vcl_ = false;
    ++stats_.resync_count;
}

size_t AnnexBReassembler::find_start_code(const std::vector<uint8_t> &buf,
                                          size_t start, size_t &code_len)
{
    if (buf.size() < 3 || start >= buf.size())
    {
        return std::string::npos;
    }
    for (size_t i = start; i + 3 <= buf.size(); ++i)
    {
        if (buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1)
        {
            code_len = 3;
            return i;
        }
        if (i + 4 <= buf.size() && buf[i] == 0 && buf[i + 1] == 0 &&
            buf[i + 2] == 0 && buf[i + 3] == 1)
        {
            code_len = 4;
            return i;
        }
    }
    return std::string::npos;
}

} // namespace hrvtx::standalone
