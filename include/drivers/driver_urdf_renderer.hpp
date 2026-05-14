#pragma once

#include "driver_urdf_types.h"
#include "driver_urdf_loader.hpp"
#include <Ogre.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

#ifdef HAVE_OPENCV
#include <opencv2/core.hpp>
#endif

namespace drivers {

class URDFRendererPlugin {
public:
    URDFRendererPlugin();
    ~URDFRendererPlugin();

    bool initialize(const UrdfRenderConfig* config);
    void shutdown();

    UrdfPluginError loadURDF(const std::string& path);
    std::string getModelName() const;
    size_t getJointCount() const;
    std::vector<std::string> getJointNames() const;
    UrdfPluginError getJointInfo(const std::string& name, UrdfJointInfo* info) const;

    UrdfPluginError setJointAngle(const std::string& name, double angle);
    UrdfPluginError getJointAngle(const std::string& name, double* angle) const;
    UrdfPluginError setMultipleJoints(const std::vector<std::string>& names,
                                       const std::vector<double>& angles);
    UrdfPluginError resetJoints();

    UrdfPluginError setCamera(const UrdfCameraConfig& config);
    UrdfPluginError getCamera(UrdfCameraConfig* config) const;

    UrdfPluginError setRenderConfig(const UrdfRenderConfig& config);
    UrdfPluginError renderFrame();
    UrdfPluginError startContinuousRender(uint32_t target_fps);
    UrdfPluginError stopContinuousRender();
    bool isRendering() const;

    UrdfPluginError getImageBuffer(UrdfImageData* image_data);
    UrdfPluginError saveImage(const std::string& path, UrdfImageFormat format);
    UrdfPluginError copyImageBuffer(uint8_t* buffer, size_t buffer_size, size_t* bytes_written);

#ifdef HAVE_OPENCV
    cv::Mat getImageAsMat() const;
#endif

    const std::string& getLastError() const { return last_error_; }

private:
    bool initializeOgre();
    bool createRenderTexture();
    void setupScene();
    Ogre::MeshPtr loadMeshWithAssimp(const std::string& filepath, const std::string& mesh_name);
    void updateSceneFromJoints();
    void applyJointTransforms();
    void captureFrameBuffer();

    void setError(const std::string& error);
    Ogre::SceneNode* findNodeForLink(const std::string& link_name);
    void computeLinkTransform(const std::string& link_name, Ogre::Matrix4& transform);
    void buildJointTree();

    Ogre::Root* ogre_root_;
    Ogre::SceneManager* scene_mgr_;
    Ogre::Camera* camera_;
    Ogre::RenderTexture* render_texture_;
    Ogre::TexturePtr texture_ptr_;

    URDFLoader urdf_loader_;

    UrdfRenderConfig render_config_;
    UrdfCameraConfig camera_config_;

    std::vector<uint8_t> image_buffer_;
    uint32_t image_width_;
    uint32_t image_height_;

    std::unordered_map<std::string, Ogre::SceneNode*> link_nodes_;
    Ogre::SceneNode* robot_root_node_;

    std::unordered_map<std::string, std::string> joint_to_child_link_;
    std::unordered_map<std::string, std::string> joint_to_parent_link_;
    std::unordered_map<std::string, std::string> link_to_parent_joint_;

    bool initialized_;
    bool model_loaded_;
    std::string last_error_;

    std::atomic<bool> continuous_render_;
    uint32_t target_fps_;
    mutable std::mutex render_mutex_;
};

} // namespace drivers
