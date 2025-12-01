#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class HevcDecoder {
public:
    HevcDecoder() {
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        if (!codec) {
            std::cerr << "Codec not found" << std::endl;
            return;
        }

        codec_ctx_ = avcodec_alloc_context3(codec);
        if (!codec_ctx_) return;

        if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) return;

        // 新增：初始化解析器
        parser_ = av_parser_init(codec->id);
        
        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
    }

    ~HevcDecoder() {
        if (codec_ctx_) avcodec_free_context(&codec_ctx_);
        if (frame_) av_frame_free(&frame_);
        if (packet_) av_packet_free(&packet_);
        if (sws_ctx_) sws_freeContext(sws_ctx_);
        if (parser_) av_parser_close(parser_);
    }

    bool decode(const std::vector<uint8_t>& data, cv::Mat& out_img) {
        if (!codec_ctx_ || !parser_) return false;

        const uint8_t* cur_ptr = data.data();
        int cur_size = data.size();
        bool got_picture = false;

        // 使用 Parser 解析数据流
        while (cur_size > 0) {
            uint8_t* out_data = nullptr;
            int out_size = 0;
            
            // 解析器会从输入流中切分出完整的 Packet
            int len = av_parser_parse2(parser_, codec_ctx_, 
                                     &out_data, &out_size,
                                     cur_ptr, cur_size, 
                                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

            cur_ptr += len;
            cur_size -= len;

            if (out_size > 0 && out_data) {
                // 发送解析好的 Packet 给解码器
                packet_->data = out_data;
                packet_->size = out_size;
                
                int ret = avcodec_send_packet(codec_ctx_, packet_);
                if (ret < 0) {
                    // 发送失败通常是流错误，继续尝试下一段
                    continue;
                }

                // 接收解码帧
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx_, frame_);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    if (ret < 0) break;

                    // 成功解码一帧，转为 OpenCV 格式
                    // 注意：如果一次 UDP 包包含多帧，这里只会返回最后一帧，
                    // 对于实时流通常没问题，若需更严谨可用回调或队列返回多帧。
                    convert_to_mat(out_img);
                    got_picture = true;
                }
            }
        }
        return got_picture;
    }

private:
    void convert_to_mat(cv::Mat& out_img) {
        if (!sws_ctx_ || codec_ctx_->width != last_width_ || codec_ctx_->height != last_height_) {
            sws_freeContext(sws_ctx_);
            sws_ctx_ = sws_getContext(
                codec_ctx_->width, codec_ctx_->height, codec_ctx_->pix_fmt,
                codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_BGR24,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            last_width_ = codec_ctx_->width;
            last_height_ = codec_ctx_->height;
        }

        out_img.create(codec_ctx_->height, codec_ctx_->width, CV_8UC3);
        uint8_t* dest[4] = { out_img.data, nullptr, nullptr, nullptr };
        int dest_linesize[4] = { static_cast<int>(out_img.step[0]), 0, 0, 0 };
        sws_scale(sws_ctx_, frame_->data, frame_->linesize, 0, codec_ctx_->height, dest, dest_linesize);
    }

    AVCodecContext* codec_ctx_ = nullptr;
    AVCodecParserContext* parser_ = nullptr; // 新增解析器
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    int last_width_ = 0;
    int last_height_ = 0;
};