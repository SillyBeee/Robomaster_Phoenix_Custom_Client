#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace hrvtx::standalone
{

struct ReassemblerStats
{
    uint64_t nal_extracted{0};
    uint64_t au_emitted{0};
    uint64_t au_dropped{0};
    uint64_t resync_count{0};
};

class AnnexBReassembler
{
  public:
    explicit AnnexBReassembler(size_t max_buffer_bytes = 1 << 20);

    void push_chunk(const std::vector<uint8_t> &chunk);

    std::vector<std::vector<uint8_t>> take_ready_access_units();

    void flush();

    const ReassemblerStats &stats() const;

  private:
    void parse_buffer();
    void process_nal(const std::vector<uint8_t> &nal);
    void emit_current_au();
    void resync_if_oversized();
    static size_t find_start_code(const std::vector<uint8_t> &buf, size_t start,
                                  size_t &code_len);

  private:
    size_t max_buffer_bytes_;
    std::vector<uint8_t> byte_buffer_;

    std::vector<std::vector<uint8_t>> current_au_nals_;
    bool current_has_vcl_{false};

    std::vector<uint8_t> last_sps_;
    std::vector<uint8_t> last_pps_;

    std::deque<std::vector<uint8_t>> ready_aus_;
    ReassemblerStats stats_{};
};

} // namespace hrvtx::standalone
