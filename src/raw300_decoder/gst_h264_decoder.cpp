#include "raw300_decoder/gst_h264_decoder.hpp"

#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace hrvtx::standalone
{

GstH264Decoder::~GstH264Decoder()
{
    shutdown();
}

bool GstH264Decoder::reset(std::string &err)
{
    shutdown();
    static std::once_flag gst_once;
    std::call_once(gst_once, []() { gst_init(nullptr, nullptr); });

    const std::vector<std::string> pipeline_descs{
        "appsrc name=src is-live=true do-timestamp=true format=time "
        "stream-type=0 "
        "caps=video/"
        "x-h264,stream-format=(string)byte-stream,alignment=(string)au ! "
        "h264parse disable-passthrough=true ! avdec_h264 ! "
        "videoconvert ! video/x-raw,format=(string)BGR ! "
        "appsink name=sink emit-signals=false sync=false drop=true "
        "max-buffers=8",
        "appsrc name=src is-live=true do-timestamp=true format=time "
        "stream-type=0 "
        "caps=video/"
        "x-h264,stream-format=(string)byte-stream,alignment=(string)au ! "
        "avdec_h264 ! videoconvert ! video/x-raw,format=(string)BGR ! "
        "appsink name=sink emit-signals=false sync=false drop=true "
        "max-buffers=8"};

    std::string last_err = "unknown gstreamer error";
    for (const auto &pipeline_desc : pipeline_descs)
    {
        GError *gerr = nullptr;
        pipeline_ = gst_parse_launch(pipeline_desc.c_str(), &gerr);
        if (gerr != nullptr)
        {
            last_err = gerr->message ? gerr->message : "gst_parse_launch failed";
            g_error_free(gerr);
            pipeline_ = nullptr;
            continue;
        }
        if (pipeline_ == nullptr)
        {
            last_err = "gst_parse_launch returned null pipeline";
            continue;
        }

        appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
        appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
        if (appsrc_ == nullptr || appsink_ == nullptr)
        {
            last_err = "failed to acquire appsrc/appsink from pipeline";
            shutdown();
            continue;
        }
        if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) ==
            GST_STATE_CHANGE_FAILURE)
        {
            last_err = "failed to set decoder pipeline to PLAYING";
            shutdown();
            continue;
        }
        err.clear();
        return true;
    }

    err = last_err;
    return false;
}

bool GstH264Decoder::decode_access_unit(const std::vector<uint8_t> &access_unit,
                                        std::vector<cv::Mat> &frames,
                                        std::string &err)
{
    if (pipeline_ == nullptr || appsrc_ == nullptr || appsink_ == nullptr)
    {
        err = "decoder pipeline not initialized";
        return false;
    }

    GstBuffer *buffer =
        gst_buffer_new_allocate(nullptr, access_unit.size(), nullptr);
    if (buffer == nullptr)
    {
        err = "gst_buffer_new_allocate failed";
        return false;
    }
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE))
    {
        gst_buffer_unref(buffer);
        err = "gst_buffer_map write failed";
        return false;
    }
    if (!access_unit.empty())
    {
        std::memcpy(map.data, access_unit.data(), access_unit.size());
    }
    gst_buffer_unmap(buffer, &map);

    GstFlowReturn flow = GST_FLOW_OK;
    g_signal_emit_by_name(appsrc_, "push-buffer", buffer, &flow);
    gst_buffer_unref(buffer);
    if (flow != GST_FLOW_OK)
    {
        err = "appsrc push-buffer failed";
        return false;
    }

    auto pull_and_append = [&](GstSample *sample)
    {
        if (sample == nullptr)
        {
            return;
        }
        GstBuffer *b = gst_sample_get_buffer(sample);
        GstCaps *caps = gst_sample_get_caps(sample);
        if (b == nullptr || caps == nullptr)
        {
            gst_sample_unref(sample);
            return;
        }
        GstStructure *s = gst_caps_get_structure(caps, 0);
        int width = 0;
        int height = 0;
        if (!gst_structure_get_int(s, "width", &width) ||
            !gst_structure_get_int(s, "height", &height))
        {
            gst_sample_unref(sample);
            return;
        }
        GstMapInfo read_map;
        if (!gst_buffer_map(b, &read_map, GST_MAP_READ))
        {
            gst_sample_unref(sample);
            return;
        }
        const int row_bytes = width * 3;
        if (width <= 0 || height <= 0 ||
            read_map.size < static_cast<size_t>(row_bytes * height))
        {
            gst_buffer_unmap(b, &read_map);
            gst_sample_unref(sample);
            return;
        }
        cv::Mat view(height, width, CV_8UC3,
                     const_cast<guint8 *>(read_map.data), row_bytes);
        frames.push_back(view.clone());
        gst_buffer_unmap(b, &read_map);
        gst_sample_unref(sample);
    };

    constexpr GstClockTime kBlockingPull = 3 * GST_MSECOND;
    for (int round = 0; frames.empty() && round < 4; ++round)
    {
        GstSample *sample =
            gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_), kBlockingPull);
        pull_and_append(sample);
    }
    for (int i = 0; i < 32; ++i)
    {
        GstSample *sample =
            gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_), 0);
        if (sample == nullptr)
        {
            break;
        }
        pull_and_append(sample);
    }

    return true;
}

void GstH264Decoder::shutdown()
{
    if (pipeline_ != nullptr)
    {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
    if (appsrc_ != nullptr)
    {
        gst_object_unref(appsrc_);
        appsrc_ = nullptr;
    }
    if (appsink_ != nullptr)
    {
        gst_object_unref(appsink_);
        appsink_ = nullptr;
    }
    if (pipeline_ != nullptr)
    {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

} // namespace hrvtx::standalone
