#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include <urdf_model/model.h>
#include <urdf_model/types.h>

namespace urdf {
using ModelInterfaceSharedPtr = std::shared_ptr<ModelInterface>;
}

namespace drivers {

class URDFLoader {
public:
    struct VisualInfo {
        std::string mesh_path;
        urdf::Vector3 scale{1.0, 1.0, 1.0};
        urdf::Pose origin;
        urdf::Color color;
        std::string texture_filename;
    };

    bool load(const std::string& path);
    std::string getModelName() const;
    std::vector<std::string> getJointNames() const;
    std::vector<std::string> getLinkNames() const;
    std::vector<VisualInfo> getLinkVisuals(const std::string& link_name) const;
    std::optional<std::string> getURDFDirectory() const;
    bool setJointAngle(const std::string& name, double degrees);
    std::optional<double> getJointAngle(const std::string& name) const;

    urdf::ModelInterfaceSharedPtr model;

private:
    std::unordered_map<std::string, double> joint_angles;
    std::filesystem::path urdf_directory;
    std::string resolveMeshPath(const std::string& raw_path) const;
};

} // namespace drivers
