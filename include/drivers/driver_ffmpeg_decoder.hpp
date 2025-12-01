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
        // 1. 查找 HEVC 解码器
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        if (!codec) {
            std::cerr << "Codec not found" << std::endl;
            return;
        }

        // 2. 分配解码器上下文
        codec_ctx_ = avcodec_alloc_context3(codec);
        if (!codec_ctx_) {
            std::cerr << "Could not allocate video codec context" << std::endl;
            return;
        }

        // 3. 打开解码器
        if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
            std::cerr << "Could not open codec" << std::endl;
            return;
        }

        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
    }

    ~HevcDecoder() {
        if (codec_ctx_) avcodec_free_context(&codec_ctx_);
        if (frame_) av_frame_free(&frame_);
        if (packet_) av_packet_free(&packet_);
        if (sws_ctx_) sws_freeContext(sws_ctx_);
    }

    // 解码函数：输入 HEVC 裸流数据，输出 OpenCV Mat (BGR)
    bool decode(const std::vector<uint8_t>& data, cv::Mat& out_img) {
        if (!codec_ctx_) return false;

        // 填充 Packet
        packet_->data = const_cast<uint8_t*>(data.data());
        packet_->size = data.size();

        // 发送 Packet 到解码器
        int ret = avcodec_send_packet(codec_ctx_, packet_);
        if (ret < 0) {
            std::cerr << "Error sending a packet for decoding" << std::endl;
            return false;
        }

        // 接收解码后的 Frame
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx_, frame_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                return false; // 需要更多数据
            } else if (ret < 0) {
                std::cerr << "Error during decoding" << std::endl;
                return false;
            }

            // 转换 YUV420P -> BGR (OpenCV 格式)
            if (!sws_ctx_ || 
                codec_ctx_->width != last_width_ || 
                codec_ctx_->height != last_height_) {
                
                sws_freeContext(sws_ctx_);
                sws_ctx_ = sws_getContext(
                    codec_ctx_->width, codec_ctx_->height, codec_ctx_->pix_fmt,
                    codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_BGR24,
                    SWS_BILINEAR, nullptr, nullptr, nullptr
                );
                last_width_ = codec_ctx_->width;
                last_height_ = codec_ctx_->height;
            }

            // 创建 OpenCV Mat
            out_img.create(codec_ctx_->height, codec_ctx_->width, CV_8UC3);
            
            uint8_t* dest[4] = { out_img.data, nullptr, nullptr, nullptr };
            int dest_linesize[4] = { static_cast<int>(out_img.step[0]), 0, 0, 0 };

            sws_scale(sws_ctx_, frame_->data, frame_->linesize, 0, codec_ctx_->height, dest, dest_linesize);
            
            return true; // 成功解码一帧
        }
        return false;
    }

private:
    AVCodecContext* codec_ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ctx_ = nullptr;
    int last_width_ = 0;
    int last_height_ = 0;
};