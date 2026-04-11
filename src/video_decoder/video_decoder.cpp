#include "video_decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace video_decoder {

VideoDecoder::VideoDecoder() : VideoDecoder("h264") {}

VideoDecoder::VideoDecoder(std::string codec_name)
: codec_name_(std::move(codec_name))
{
}

VideoDecoder::~VideoDecoder()
{
  close();
}

bool VideoDecoder::init()
{
  return init(codec_name_);
}

bool VideoDecoder::init(const std::string & codec_name)
{
  codec_name_ = codec_name;
  close();

  const AVCodec * codec = avcodec_find_decoder_by_name(codec_name_.c_str());
  if (!codec) {
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    if (codec_name_ == "h264") {
      codec_id = AV_CODEC_ID_H264;
    } else if (codec_name_ == "h265" || codec_name_ == "hevc") {
      codec_id = AV_CODEC_ID_HEVC;
    }
    if (codec_id != AV_CODEC_ID_NONE) {
      codec = avcodec_find_decoder(codec_id);
    }
  }

  if (!codec) {
    recordError("Decoder not found: " + codec_name_);
    return false;
  }

  parser_ = av_parser_init(codec->id);
  if (!parser_) {
    recordError("Failed to create parser");
    close();
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(codec);
  if (!codec_ctx_) {
    recordError("Failed to allocate codec context");
    close();
    return false;
  }

  codec_ctx_->thread_type = FF_THREAD_FRAME;
  codec_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;

  const int open_ret = avcodec_open2(codec_ctx_, codec, nullptr);
  if (open_ret < 0) {
    recordError("Failed to open codec", open_ret);
    close();
    return false;
  }

  frame_ = av_frame_alloc();
  packet_ = av_packet_alloc();
  if (!frame_ || !packet_) {
    recordError("Failed to allocate frame/packet");
    close();
    return false;
  }

  initialized_ = true;
  last_error_.clear();
  return true;
}

void VideoDecoder::reset(const std::string & reason)
{
  close();
  init(codec_name_);
  if (!reason.empty()) {
    last_error_.clear();
  }
}

void VideoDecoder::close()
{
  if (parser_) {
    av_parser_close(parser_);
    parser_ = nullptr;
  }

  if (codec_ctx_) {
    avcodec_free_context(&codec_ctx_);
  }

  if (frame_) {
    av_frame_free(&frame_);
  }

  if (packet_) {
    av_packet_free(&packet_);
  }

  if (sws_ctx_) {
    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;
  }

  sws_width_ = 0;
  sws_height_ = 0;
  sws_src_format_ = -1;

  initialized_ = false;
  has_last_sequence_ = false;
  clearFrames();
}

bool VideoDecoder::pushPacket(const uint8_t * data, size_t size, uint64_t sequence_id)
{
  if (!data || size == 0) {
    return false;
  }

  if (!ensureInitialized()) {
    return false;
  }

  packet_count_++;

  if (has_last_sequence_ && sequence_id != last_sequence_id_ + 1) {
    gap_count_++;
    reset("sequence gap");
    if (!ensureInitialized()) {
      return false;
    }
  }

  last_sequence_id_ = sequence_id;
  has_last_sequence_ = true;

  const uint8_t * data_ptr = data;
  int data_size = static_cast<int>(size);

  while (data_size > 0) {
    int parsed = av_parser_parse2(
      parser_, codec_ctx_, &packet_->data, &packet_->size,
      data_ptr, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

    if (parsed < 0) {
      recordError("Parser error", parsed);
      return false;
    }

    data_ptr += parsed;
    data_size -= parsed;

    if (packet_->size > 0) {
      parsed_packet_count_++;
      if (!decodePacket()) {
        return false;
      }
    }
  }

  return true;
}

bool VideoDecoder::pushPacket(const std::vector<uint8_t> & packet, uint64_t sequence_id)
{
  return pushPacket(packet.data(), packet.size(), sequence_id);
}

bool VideoDecoder::hasFrame() const
{
  return !frame_queue_.empty();
}

bool VideoDecoder::getFrame(VideoFrame * out)
{
  if (!out || frame_queue_.empty()) {
    return false;
  }

  *out = std::move(frame_queue_.front());
  frame_queue_.pop_front();
  return true;
}

size_t VideoDecoder::pendingFrameCount() const
{
  return frame_queue_.size();
}

size_t VideoDecoder::packetCount() const
{
  return packet_count_;
}

size_t VideoDecoder::parsedPacketCount() const
{
  return parsed_packet_count_;
}

size_t VideoDecoder::frameCount() const
{
  return frame_count_;
}

size_t VideoDecoder::gapCount() const
{
  return gap_count_;
}

const std::string & VideoDecoder::lastError() const
{
  return last_error_;
}

const std::string & VideoDecoder::codecName() const
{
  return codec_name_;
}

bool VideoDecoder::decodePacket()
{
  const int send_ret = avcodec_send_packet(codec_ctx_, packet_);
  if (send_ret < 0) {
    recordError("Decode send failed", send_ret);
    return false;
  }

  while (true) {
    const int receive_ret = avcodec_receive_frame(codec_ctx_, frame_);
    if (receive_ret == AVERROR(EAGAIN) || receive_ret == AVERROR_EOF) {
      break;
    }
    if (receive_ret < 0) {
      recordError("Decode receive failed", receive_ret);
      return false;
    }

    if (frame_->width <= 0 || frame_->height <= 0) {
      continue;
    }

    const int width = frame_->width;
    const int height = frame_->height;
    const int src_format = frame_->format;

    if (!sws_ctx_ || width != sws_width_ || height != sws_height_ || src_format != sws_src_format_) {
      sws_ctx_ = sws_getCachedContext(
        sws_ctx_, width, height, static_cast<AVPixelFormat>(src_format),
        width, height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr);
      sws_width_ = width;
      sws_height_ = height;
      sws_src_format_ = src_format;
      if (!sws_ctx_) {
        recordError("Failed to init sws context");
        return false;
      }
    }

    VideoFrame out;
    out.width = width;
    out.height = height;
    out.pts = frame_->pts;
    out.format = PixelFormat::BGR24;
    out.data.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);

    uint8_t * dst_data[4] = {out.data.data(), nullptr, nullptr, nullptr};
    int dst_linesize[4] = {width * 3, 0, 0, 0};

    sws_scale(
      sws_ctx_, frame_->data, frame_->linesize, 0, height, dst_data, dst_linesize);

    frame_queue_.push_back(std::move(out));
    if (frame_queue_.size() > max_queue_size_) {
      frame_queue_.pop_front();
    }
    frame_count_++;
  }

  return true;
}

void VideoDecoder::recordError(const std::string & message, int ffmpeg_error)
{
  if (ffmpeg_error == 0) {
    last_error_ = message;
    return;
  }

  char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(ffmpeg_error, errbuf, sizeof(errbuf));
  last_error_ = message + ": " + errbuf;
}

bool VideoDecoder::ensureInitialized()
{
  if (initialized_) {
    return true;
  }
  return init(codec_name_);
}

void VideoDecoder::clearFrames()
{
  frame_queue_.clear();
}

}  // namespace video_decoder
