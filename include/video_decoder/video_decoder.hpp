#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVCodecParserContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace video_decoder {

class VideoDecoder {
 public:
  enum class PixelFormat {
    BGR24
  };

  struct VideoFrame {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::BGR24;
    int64_t pts = -1;
    std::vector<uint8_t> data;
  };

  VideoDecoder();
  explicit VideoDecoder(std::string codec_name);
  ~VideoDecoder();

  VideoDecoder(const VideoDecoder &) = delete;
  VideoDecoder & operator=(const VideoDecoder &) = delete;
  VideoDecoder(VideoDecoder &&) = delete;
  VideoDecoder & operator=(VideoDecoder &&) = delete;

  bool init();
  bool init(const std::string & codec_name);
  void reset(const std::string & reason = std::string());
  void close();

  bool pushPacket(const uint8_t * data, size_t size, uint64_t sequence_id);
  bool pushPacket(const std::vector<uint8_t> & packet, uint64_t sequence_id);

  bool hasFrame() const;
  bool getFrame(VideoFrame * out);
  size_t pendingFrameCount() const;

  size_t packetCount() const;
  size_t parsedPacketCount() const;
  size_t frameCount() const;
  size_t gapCount() const;

  const std::string & lastError() const;
  const std::string & codecName() const;

 private:
  bool decodePacket();
  void recordError(const std::string & message, int ffmpeg_error = 0);
  bool ensureInitialized();
  void clearFrames();

  std::string codec_name_;
  bool initialized_ = false;
  uint64_t last_sequence_id_ = 0;
  bool has_last_sequence_ = false;

  size_t packet_count_ = 0;
  size_t parsed_packet_count_ = 0;
  size_t frame_count_ = 0;
  size_t gap_count_ = 0;

  std::deque<VideoFrame> frame_queue_;
  size_t max_queue_size_ = 3;

  std::string last_error_;

  AVCodecContext * codec_ctx_ = nullptr;
  AVCodecParserContext * parser_ = nullptr;
  AVFrame * frame_ = nullptr;
  AVPacket * packet_ = nullptr;
  SwsContext * sws_ctx_ = nullptr;
  int sws_width_ = 0;
  int sws_height_ = 0;
  int sws_src_format_ = -1;
};

}  // namespace video_decoder
