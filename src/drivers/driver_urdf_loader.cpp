#include "driver_urdf_loader.hpp"

#include <urdf_parser/urdf_parser.h>

namespace drivers {

bool URDFLoader::load(const std::string& path) {
    auto parsed = urdf::parseURDFFile(path);
    if (!parsed) {
        return false;
    }
    model = parsed;
    joint_angles.clear();
    urdf_directory = std::filesystem::path(path).parent_path();
    for (const auto& [name, joint] : model->joints_) {
        (void)joint;
        joint_angles[name] = 0.0;
    }
    return true;
}

std::string URDFLoader::getModelName() const {
    if (model) {
        return model->getName();
    }
    return {};
}

std::vector<std::string> URDFLoader::getJointNames() const {
    std::vector<std::string> names;
    names.reserve(joint_angles.size());
    for (const auto& [name, _] : joint_angles) {
        names.push_back(name);
    }
    return names;
}

bool URDFLoader::setJointAngle(const std::string& name, double degrees) {
    const auto it = joint_angles.find(name);
    if (it == joint_angles.end()) {
        return false;
    }
    it->second = degrees;
    return true;
}

std::optional<double> URDFLoader::getJointAngle(const std::string& name) const {
    const auto it = joint_angles.find(name);
    if (it == joint_angles.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> URDFLoader::getLinkNames() const {
    std::vector<std::string> names;
    if (!model) {
        return names;
    }
    names.reserve(model->links_.size());
    for (const auto& [name, link] : model->links_) {
        (void)link;
        names.push_back(name);
    }
    return names;
}

std::vector<URDFLoader::VisualInfo> URDFLoader::getLinkVisuals(const std::string& link_name) const {
    std::vector<VisualInfo> visuals;
    if (!model) {
        return visuals;
    }
    const auto it = model->links_.find(link_name);
    if (it == model->links_.end()) {
        return visuals;
    }
    auto addVisual = [&visuals, this](const urdf::VisualSharedPtr& visual) {
        if (!visual || !visual->geometry || visual->geometry->type != urdf::Geometry::MESH) {
            return;
        }
        auto mesh = std::dynamic_pointer_cast<urdf::Mesh>(visual->geometry);
        if (!mesh || mesh->filename.empty()) {
            return;
        }
        VisualInfo info;
        info.mesh_path = resolveMeshPath(mesh->filename);
        info.scale = mesh->scale;
        info.origin = visual->origin;

        if (visual->material) {
            info.color = visual->material->color;
            info.texture_filename = visual->material->texture_filename;
        } else {
            info.color.r = 0.8;
            info.color.g = 0.8;
            info.color.b = 0.8;
            info.color.a = 1.0;
        }

        visuals.push_back(info);
    };
    addVisual(it->second->visual);
    for (const auto& visual : it->second->visual_array) {
        addVisual(visual);
    }
    return visuals;
}

std::optional<std::string> URDFLoader::getURDFDirectory() const {
    if (urdf_directory.empty()) {
        return std::nullopt;
    }
    return urdf_directory.string();
}

std::string URDFLoader::resolveMeshPath(const std::string& raw_path) const {
    if (raw_path.empty()) {
        return {};
    }

    if (raw_path.rfind("package://", 0) == 0) {
        std::string package_path = raw_path.substr(10);

        size_t slash_pos = package_path.find('/');
        if (slash_pos != std::string::npos) {
            std::string package_name = package_path.substr(0, slash_pos);
            std::string relative_path = package_path.substr(slash_pos + 1);

            if (!urdf_directory.empty()) {
                std::filesystem::path search_path = urdf_directory;
                for (int i = 0; i < 3; ++i) {
                    std::filesystem::path candidate = search_path / package_name / relative_path;
                    if (std::filesystem::exists(candidate)) {
                        try {
                            return std::filesystem::canonical(candidate).string();
                        } catch (...) {
                            return std::filesystem::absolute(candidate).string();
                        }
                    }
                    search_path = search_path.parent_path();
                    if (search_path.empty()) break;
                }

                std::filesystem::path parent_candidate = urdf_directory.parent_path() / package_name / relative_path;
                if (std::filesystem::exists(parent_candidate)) {
                    try {
                        return std::filesystem::canonical(parent_candidate).string();
                    } catch (...) {
                        return std::filesystem::absolute(parent_candidate).string();
                    }
                }

                std::filesystem::path meshes_candidate = urdf_directory.parent_path() / relative_path;
                if (std::filesystem::exists(meshes_candidate)) {
                    try {
                        return std::filesystem::canonical(meshes_candidate).string();
                    } catch (...) {
                        return std::filesystem::absolute(meshes_candidate).string();
                    }
                }
            }
        }
        return raw_path;
    }

    std::filesystem::path candidate(raw_path);
    if (candidate.is_absolute()) {
        if (std::filesystem::exists(candidate)) {
            try {
                return std::filesystem::canonical(candidate).string();
            } catch (...) {
                return raw_path;
            }
        }
        return raw_path;
    }

    if (urdf_directory.empty()) {
        return raw_path;
    }

    std::filesystem::path resolved = urdf_directory / candidate;
    if (std::filesystem::exists(resolved)) {
        try {
            return std::filesystem::canonical(resolved).string();
        } catch (...) {
            return std::filesystem::absolute(resolved).string();
        }
    }

    return (urdf_directory / candidate).string();
}

} // namespace drivers
