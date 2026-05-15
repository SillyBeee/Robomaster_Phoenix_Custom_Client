#include "driver_urdf_renderer.hpp"
#include "utils_cv.hpp"
#include <opencv2/imgcodecs.hpp>
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    std::string urdf_path;
    if (argc >= 2) {
        urdf_path = argv[1];
    } else {
        auto src = std::filesystem::path(PROJECT_SOURCE_DIR);
        urdf_path = (src / "arm_description/urdf/Engineer2.urdf").string();
        if (!std::filesystem::exists(urdf_path))
            urdf_path = (src / "urdf_renderer/arm_description/urdf/Engineer2.urdf").string();
    }
    std::cout << "URDF path: " << urdf_path << std::endl;

    if (!std::filesystem::exists(urdf_path)) {
        std::cerr << "URDF file not found: " << urdf_path << std::endl;
        return 1;
    }

    drivers::URDFRendererPlugin plugin;
    drivers::UrdfRenderConfig cfg;
    cfg.width = 800;
    cfg.height = 600;
    cfg.transparent_background = true;
    cfg.anti_aliasing = 0;

    if (!plugin.initialize(&cfg)) {
        std::cerr << "INIT FAILED: " << plugin.getLastError() << std::endl;
        return 1;
    }
    std::cout << "INIT OK" << std::endl;

    if (plugin.loadURDF(urdf_path) != drivers::URDF_SUCCESS) {
        std::cerr << "LOAD FAILED: " << plugin.getLastError() << std::endl;
        return 1;
    }
    std::cout << "LOAD OK: " << urdf_path << std::endl;

    drivers::UrdfCameraConfig cam;
    cam.position[0] = -1.0f; cam.position[1] = -0.5f; cam.position[2] = 3.0f;
    cam.look_at[0] = 0.0f; cam.look_at[1] = 0.0f; cam.look_at[2] = 0.5f;
    cam.up[0] = 0.0f; cam.up[1] = 0.0f; cam.up[2] = 1.0f;
    cam.fov_degrees = 50.0f;
    cam.near_clip = 0.02f;
    cam.far_clip = 20.0f;
    plugin.setCamera(cam);

    if (plugin.renderFrame() != drivers::URDF_SUCCESS) {
        std::cerr << "RENDER FAILED: " << plugin.getLastError() << std::endl;
        return 1;
    }
    std::cout << "RENDER OK" << std::endl;

    cv::Mat rgba = plugin.getImageAsMat();
    std::cout << "RGBA: " << rgba.cols << "x" << rgba.rows
              << " channels=" << rgba.channels() << " type=" << rgba.type() << std::endl;

    double minVal, maxVal;
    cv::minMaxLoc(rgba.reshape(1), &minVal, &maxVal);
    std::cout << "RGBA pixel range: min=" << minVal << " max=" << maxVal << std::endl;

    cv::imwrite("urdf_test_original.png", rgba);

    cv::Mat bgr;
    cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
    cv::imwrite("urdf_test_bgr.png", bgr);

    cv::Mat rgb;
    cv::cvtColor(rgba, rgb, cv::COLOR_RGBA2RGB);
    cv::imwrite("urdf_test_rgb.png", rgb);

    auto slint_img = MatRgbaToSlintImage(rgba);
    auto size = slint_img.size();
    std::cout << "Slint image: " << size.width << "x" << size.height << std::endl;

    std::cout << "ALL DONE. Check urdf_test_*.png" << std::endl;
    return 0;
}
