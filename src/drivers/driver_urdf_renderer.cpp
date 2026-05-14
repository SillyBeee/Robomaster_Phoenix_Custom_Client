#include "driver_urdf_renderer.hpp"
#include <urdf_parser/urdf_parser.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/DefaultLogger.hpp>
#include <iostream>
#include <fstream>
#include <set>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cmath>

namespace drivers {

URDFRendererPlugin::URDFRendererPlugin()
    : ogre_root_(nullptr)
    , scene_mgr_(nullptr)
    , camera_(nullptr)
    , render_texture_(nullptr)
    , robot_root_node_(nullptr)
    , image_width_(800)
    , image_height_(600)
    , initialized_(false)
    , model_loaded_(false)
    , continuous_render_(false)
    , target_fps_(30)
{
    camera_config_.position[0] = 0.22f;
    camera_config_.position[1] = 0.10f;
    camera_config_.position[2] = 0.16f;
    camera_config_.look_at[0] = 0.0f;
    camera_config_.look_at[1] = 0.0f;
    camera_config_.look_at[2] = 0.11f;
    camera_config_.up[0] = 0.0f;
    camera_config_.up[1] = 0.0f;
    camera_config_.up[2] = 1.0f;
    camera_config_.fov_degrees = 22.0f;
    camera_config_.near_clip = 0.02f;
    camera_config_.far_clip = 5.0f;

    render_config_.width = 800;
    render_config_.height = 600;
    render_config_.transparent_background = true;
    render_config_.background_color[0] = 0.0f;
    render_config_.background_color[1] = 0.0f;
    render_config_.background_color[2] = 0.0f;
    render_config_.background_color[3] = 0.0f;
    render_config_.anti_aliasing = 0;
}

URDFRendererPlugin::~URDFRendererPlugin() {
    shutdown();
}

bool URDFRendererPlugin::initialize(const UrdfRenderConfig* config) {
    if (initialized_) {
        setError("Plugin already initialized");
        return false;
    }

    if (config) {
        render_config_ = *config;
        image_width_ = config->width;
        image_height_ = config->height;
    }

    if (!initializeOgre()) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool URDFRendererPlugin::initializeOgre() {
    int saved_stdout = dup(STDOUT_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    dup2(devnull, STDOUT_FILENO);

    bool result;
    try {
        std::ofstream plugins_cfg("plugins_headless.cfg");
        plugins_cfg << "PluginFolder=/usr/lib/x86_64-linux-gnu/OGRE\n";
        plugins_cfg << "Plugin=RenderSystem_GL\n";
        plugins_cfg.close();

        ogre_root_ = new Ogre::Root("plugins_headless.cfg", "", "");
        Ogre::LogManager::getSingleton().getDefaultLog()->setDebugOutputEnabled(false);
        Assimp::DefaultLogger::set(new Assimp::NullLogger);

        const Ogre::RenderSystemList& render_systems = ogre_root_->getAvailableRenderers();
        if (render_systems.empty()) {
            setError("No render systems available");
            result = false;
            goto cleanup;
        }

        Ogre::RenderSystem* render_system = render_systems[0];

        render_system->setConfigOption("Full Screen", "No");
        render_system->setConfigOption("VSync", "No");
        render_system->setConfigOption("RTT Preferred Mode", "FBO");

        ogre_root_->setRenderSystem(render_system);

        Ogre::NameValuePairList params;
        params["hidden"] = "true";
        params["FSAA"] = "0";
        params["vsync"] = "false";
        params["border"] = "none";
        params["left"] = "-10000";
        params["top"] = "-10000";

        ogre_root_->initialise(false);
        Ogre::RenderWindow* window = ogre_root_->createRenderWindow(
            "URDFRendererHiddenWindow", 1, 1, false, &params);
        window->setVisible(false);
        window->setAutoUpdated(false);

        scene_mgr_ = ogre_root_->createSceneManager("DefaultSceneManager", "MainSceneManager");

        camera_ = scene_mgr_->createCamera("MainCamera");
        camera_->setNearClipDistance(camera_config_.near_clip);
        camera_->setFarClipDistance(camera_config_.far_clip);
        camera_->setFOVy(Ogre::Degree(camera_config_.fov_degrees));

        Ogre::SceneNode* camNode = scene_mgr_->getRootSceneNode()->createChildSceneNode("MainCameraNode");
        camNode->attachObject(camera_);
        camNode->setPosition(camera_config_.position[0],
                            camera_config_.position[1],
                            camera_config_.position[2]);
        camNode->lookAt(Ogre::Vector3(camera_config_.look_at[0],
                       camera_config_.look_at[1],
                       camera_config_.look_at[2]), Ogre::Node::TS_WORLD);

        scene_mgr_->setAmbientLight(Ogre::ColourValue(0.8f, 0.8f, 0.8f));
        Ogre::Light* light = scene_mgr_->createLight("MainLight");
        light->setType(Ogre::Light::LT_DIRECTIONAL);

        Ogre::SceneNode* lightNode = scene_mgr_->getRootSceneNode()->createChildSceneNode("MainLightNode");
        lightNode->attachObject(light);
        lightNode->setDirection(Ogre::Vector3(-1, -1, -1).normalisedCopy());

        light->setDiffuseColour(Ogre::ColourValue::White);
        light->setSpecularColour(Ogre::ColourValue(0.4f, 0.4f, 0.4f));

        Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton().create(
            "URDFDefaultMaterial",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        material->getTechnique(0)->getPass(0)->setDiffuse(0.8f, 0.8f, 0.8f, 1.0f);
        material->getTechnique(0)->getPass(0)->setAmbient(0.8f, 0.8f, 0.8f);
        material->getTechnique(0)->getPass(0)->setSpecular(0.3f, 0.3f, 0.3f, 1.0f);
        material->getTechnique(0)->getPass(0)->setShininess(25.0f);
        material->setLightingEnabled(true);

        if (!createRenderTexture()) {
            result = false;
            goto cleanup;
        }

        result = true;
    } catch (const Ogre::Exception& e) {
        setError(std::string("Ogre initialization failed: ") + e.getDescription());
        result = false;
    }

cleanup:
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    close(devnull);
    return result;
}

bool URDFRendererPlugin::createRenderTexture() {
    try {
        Ogre::PixelFormat pixel_format = render_config_.transparent_background
            ? Ogre::PF_BYTE_RGBA
            : Ogre::PF_BYTE_RGB;

        texture_ptr_ = Ogre::TextureManager::getSingleton().createManual(
            "RenderTexture",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
            Ogre::TEX_TYPE_2D,
            image_width_,
            image_height_,
            0,
            pixel_format,
            Ogre::TU_RENDERTARGET);

        render_texture_ = texture_ptr_->getBuffer()->getRenderTarget();

        Ogre::Viewport* viewport = render_texture_->addViewport(camera_);

        if (render_config_.transparent_background) {
            viewport->setBackgroundColour(Ogre::ColourValue(0, 0, 0, 0));
            viewport->setClearEveryFrame(true, Ogre::FBT_COLOUR | Ogre::FBT_DEPTH);
        } else {
            viewport->setBackgroundColour(Ogre::ColourValue(
                render_config_.background_color[0],
                render_config_.background_color[1],
                render_config_.background_color[2],
                render_config_.background_color[3]));
        }

        camera_->setAspectRatio(
            static_cast<float>(image_width_) / static_cast<float>(image_height_));

        return true;
    } catch (const Ogre::Exception& e) {
        setError(std::string("Failed to create render texture: ") + e.getDescription());
        return false;
    }
}

void URDFRendererPlugin::shutdown() {
    if (continuous_render_) {
        stopContinuousRender();
    }

    link_nodes_.clear();

    if (scene_mgr_) {
        scene_mgr_->clearScene();
    }

    if (render_texture_) {
        render_texture_ = nullptr;
    }

    if (texture_ptr_) {
        Ogre::TextureManager::getSingleton().remove(texture_ptr_);
        texture_ptr_.reset();
    }

    if (ogre_root_) {
        ogre_root_->shutdown();
        delete ogre_root_;
        ogre_root_ = nullptr;
    }

    initialized_ = false;
    model_loaded_ = false;
}

UrdfPluginError URDFRendererPlugin::loadURDF(const std::string& path) {
    if (!initialized_) {
        setError("Plugin not initialized");
        return URDF_ERROR_INIT_FAILED;
    }

    if (!urdf_loader_.load(path)) {
        setError("Failed to load URDF file");
        return URDF_ERROR_PARSE_FAILED;
    }

    scene_mgr_->clearScene();
    link_nodes_.clear();

    scene_mgr_->setAmbientLight(Ogre::ColourValue(0.8f, 0.8f, 0.8f));
    Ogre::Light* light = scene_mgr_->createLight("MainLight");
    light->setType(Ogre::Light::LT_DIRECTIONAL);

    Ogre::SceneNode* lightNode = scene_mgr_->getRootSceneNode()->createChildSceneNode("MainLightNode");
    lightNode->attachObject(light);
    lightNode->setDirection(Ogre::Vector3(-1, -1, -1).normalisedCopy());

    light->setDiffuseColour(Ogre::ColourValue::White);

    if (camera_) {
        Ogre::SceneNode* camNode = scene_mgr_->getRootSceneNode()->createChildSceneNode("MainCameraNode");
        camNode->attachObject(camera_);
        camNode->setPosition(camera_config_.position[0],
                            camera_config_.position[1],
                            camera_config_.position[2]);
        camNode->lookAt(Ogre::Vector3(camera_config_.look_at[0],
                       camera_config_.look_at[1],
                       camera_config_.look_at[2]), Ogre::Node::TS_WORLD);
    }

    if (const auto dir = urdf_loader_.getURDFDirectory()) {
        try {
            Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
                *dir, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

            std::filesystem::path urdf_dir_path(*dir);
            std::filesystem::path parent = urdf_dir_path.parent_path();
            if (!parent.empty() && std::filesystem::exists(parent)) {
                Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
                    parent.string(), "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, true);
            }

            std::filesystem::path meshes_dir = parent / "meshes";
            if (std::filesystem::exists(meshes_dir)) {
                Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
                    meshes_dir.string(), "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
            }
        } catch (const std::exception& e) {
        }
    }

    try {
        Ogre::ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
    } catch (const Ogre::Exception& e) {
    }

    setupScene();

    buildJointTree();

    model_loaded_ = true;
    return URDF_SUCCESS;
}

Ogre::MeshPtr URDFRendererPlugin::loadMeshWithAssimp(const std::string& filepath, const std::string& mesh_name) {

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filepath,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenNormals);

    if (!scene || !scene->HasMeshes()) {
        return Ogre::MeshPtr();
    }

    Ogre::MeshPtr ogreMesh = Ogre::MeshManager::getSingleton().createManual(
        mesh_name,
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    const aiMesh* ai_mesh = scene->mMeshes[0];

    Ogre::SubMesh* subMesh = ogreMesh->createSubMesh();
    subMesh->useSharedVertices = false;
    subMesh->vertexData = new Ogre::VertexData();
    subMesh->vertexData->vertexCount = ai_mesh->mNumVertices;

    Ogre::VertexDeclaration* decl = subMesh->vertexData->vertexDeclaration;
    size_t offset = 0;
    decl->addElement(0, offset, Ogre::VET_FLOAT3, Ogre::VES_POSITION);
    offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
    decl->addElement(0, offset, Ogre::VET_FLOAT3, Ogre::VES_NORMAL);
    offset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);

    Ogre::HardwareVertexBufferSharedPtr vbuf =
        Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
            offset,
            ai_mesh->mNumVertices,
            Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY);

    float* vertices = static_cast<float*>(vbuf->lock(Ogre::HardwareBuffer::HBL_DISCARD));

    for (unsigned int i = 0; i < ai_mesh->mNumVertices; ++i) {
        vertices[i * 6 + 0] = ai_mesh->mVertices[i].x;
        vertices[i * 6 + 1] = ai_mesh->mVertices[i].y;
        vertices[i * 6 + 2] = ai_mesh->mVertices[i].z;
        if (ai_mesh->HasNormals()) {
            vertices[i * 6 + 3] = ai_mesh->mNormals[i].x;
            vertices[i * 6 + 4] = ai_mesh->mNormals[i].y;
            vertices[i * 6 + 5] = ai_mesh->mNormals[i].z;
        } else {
            vertices[i * 6 + 3] = 0.0f;
            vertices[i * 6 + 4] = 0.0f;
            vertices[i * 6 + 5] = 1.0f;
        }
    }

    vbuf->unlock();
    subMesh->vertexData->vertexBufferBinding->setBinding(0, vbuf);

    subMesh->indexData->indexCount = ai_mesh->mNumFaces * 3;
    subMesh->indexData->indexBuffer =
        Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
            Ogre::HardwareIndexBuffer::IT_32BIT,
            subMesh->indexData->indexCount,
            Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY);

    unsigned int* indices = static_cast<unsigned int*>(
        subMesh->indexData->indexBuffer->lock(Ogre::HardwareBuffer::HBL_DISCARD));

    for (unsigned int i = 0; i < ai_mesh->mNumFaces; ++i) {
        const aiFace& face = ai_mesh->mFaces[i];
        indices[i * 3 + 0] = face.mIndices[0];
        indices[i * 3 + 1] = face.mIndices[1];
        indices[i * 3 + 2] = face.mIndices[2];
    }

    subMesh->indexData->indexBuffer->unlock();

    ogreMesh->_setBounds(Ogre::AxisAlignedBox(
        -100, -100, -100, 100, 100, 100));
    ogreMesh->_setBoundingSphereRadius(100.0f);

    ogreMesh->load();

    return ogreMesh;
}

void URDFRendererPlugin::setupScene() {
    robot_root_node_ = scene_mgr_->getRootSceneNode()->createChildSceneNode("robot_root");

    robot_root_node_->setOrientation(Ogre::Quaternion(Ogre::Degree(-90), Ogre::Vector3::UNIT_X));

    const auto link_names = urdf_loader_.getLinkNames();

    std::set<std::string> mesh_dirs;

    for (const auto& link_name : link_names) {
        const auto visuals = urdf_loader_.getLinkVisuals(link_name);

        if (visuals.empty()) {
            continue;
        }

        Ogre::SceneNode* link_node = robot_root_node_->createChildSceneNode(link_name);
        link_nodes_[link_name] = link_node;

        for (size_t idx = 0; idx < visuals.size(); ++idx) {
            const auto& visual = visuals[idx];
            if (visual.mesh_path.empty()) {
                continue;
            }

            std::filesystem::path mesh_path_obj(visual.mesh_path);
            std::string mesh_dir = mesh_path_obj.parent_path().string();
            std::string mesh_filename = mesh_path_obj.filename().string();

            if (!mesh_dir.empty() && mesh_dirs.find(mesh_dir) == mesh_dirs.end()) {
                try {
                    Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
                        mesh_dir, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
                    Ogre::ResourceGroupManager::getSingleton().initialiseResourceGroup(
                        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
                    mesh_dirs.insert(mesh_dir);
                } catch (const Ogre::Exception& e) {
                }
            }

            const std::string entity_name = link_name + "_entity_" + std::to_string(idx);

            try {
                std::string extension = mesh_path_obj.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

                Ogre::Entity* entity = nullptr;

                if (extension == ".stl") {
                    std::string mesh_resource_name = entity_name + "_mesh";
                    Ogre::MeshPtr mesh = loadMeshWithAssimp(visual.mesh_path, mesh_resource_name);
                    if (mesh) {
                        entity = scene_mgr_->createEntity(entity_name, mesh_resource_name);
                    }
                } else {
                    entity = scene_mgr_->createEntity(entity_name, mesh_filename);
                }

                if (!entity) {
                    continue;
                }

                std::string material_name = entity_name + "_material";
                Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton().create(
                    material_name,
                    Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

                material->getTechnique(0)->getPass(0)->setDiffuse(
                    visual.color.r, visual.color.g, visual.color.b, visual.color.a);
                material->getTechnique(0)->getPass(0)->setAmbient(
                    visual.color.r * 0.8f, visual.color.g * 0.8f, visual.color.b * 0.8f);
                material->getTechnique(0)->getPass(0)->setSpecular(0.3f, 0.3f, 0.3f, 1.0f);
                material->getTechnique(0)->getPass(0)->setShininess(25.0f);
                material->setLightingEnabled(true);

                if (!visual.texture_filename.empty()) {
                    try {
                        Ogre::TextureUnitState* texUnit =
                            material->getTechnique(0)->getPass(0)->createTextureUnitState(visual.texture_filename);
                        texUnit->setTextureFiltering(Ogre::TFO_BILINEAR);
                    } catch (const Ogre::Exception& e) {
                    }
                }

                entity->setMaterialName(material_name);

                Ogre::SceneNode* visual_node = link_node->createChildSceneNode();
                visual_node->attachObject(entity);
                visual_node->setScale(visual.scale.x, visual.scale.y, visual.scale.z);
                visual_node->setPosition(
                    visual.origin.position.x,
                    visual.origin.position.y,
                    visual.origin.position.z);
                visual_node->setOrientation(
                    Ogre::Quaternion(
                        visual.origin.rotation.w,
                        visual.origin.rotation.x,
                        visual.origin.rotation.y,
                        visual.origin.rotation.z));
            } catch (const Ogre::Exception& e) {
            }
        }
    }

    applyJointTransforms();
}

std::string URDFRendererPlugin::getModelName() const {
    return urdf_loader_.getModelName();
}

size_t URDFRendererPlugin::getJointCount() const {
    return urdf_loader_.getJointNames().size();
}

std::vector<std::string> URDFRendererPlugin::getJointNames() const {
    return urdf_loader_.getJointNames();
}

UrdfPluginError URDFRendererPlugin::getJointInfo(const std::string& name, UrdfJointInfo* info) const {
    if (!model_loaded_) {
        return URDF_ERROR_NO_MODEL_LOADED;
    }

    if (!info) {
        return URDF_ERROR_INVALID_PARAMETER;
    }

    auto angle = urdf_loader_.getJointAngle(name);
    if (!angle) {
        return URDF_ERROR_JOINT_NOT_FOUND;
    }

    strncpy(info->name, name.c_str(), sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    info->current_angle = *angle;

    if (urdf_loader_.model && urdf_loader_.model->joints_.count(name)) {
        auto joint = urdf_loader_.model->joints_.at(name);
        if (joint->limits) {
            info->min_limit = joint->limits->lower;
            info->max_limit = joint->limits->upper;
        } else {
            info->min_limit = -M_PI;
            info->max_limit = M_PI;
        }
        info->joint_type = static_cast<int>(joint->type);
    }

    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::setJointAngle(const std::string& name, double angle) {
    if (!model_loaded_) {
        return URDF_ERROR_NO_MODEL_LOADED;
    }

    if (!urdf_loader_.setJointAngle(name, angle)) {
        return URDF_ERROR_JOINT_NOT_FOUND;
    }

    applyJointTransforms();
    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::getJointAngle(const std::string& name, double* angle) const {
    if (!model_loaded_) {
        return URDF_ERROR_NO_MODEL_LOADED;
    }

    if (!angle) {
        return URDF_ERROR_INVALID_PARAMETER;
    }

    auto result = urdf_loader_.getJointAngle(name);
    if (!result) {
        return URDF_ERROR_JOINT_NOT_FOUND;
    }

    *angle = *result;
    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::setMultipleJoints(
    const std::vector<std::string>& names,
    const std::vector<double>& angles) {

    if (!model_loaded_) {
        return URDF_ERROR_NO_MODEL_LOADED;
    }

    if (names.size() != angles.size()) {
        return URDF_ERROR_INVALID_PARAMETER;
    }

    for (size_t i = 0; i < names.size(); ++i) {
        if (!urdf_loader_.setJointAngle(names[i], angles[i])) {
            return URDF_ERROR_JOINT_NOT_FOUND;
        }
    }

    applyJointTransforms();
    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::resetJoints() {
    if (!model_loaded_) {
        return URDF_ERROR_NO_MODEL_LOADED;
    }

    const auto joint_names = urdf_loader_.getJointNames();
    for (const auto& name : joint_names) {
        urdf_loader_.setJointAngle(name, 0.0);
    }

    applyJointTransforms();
    return URDF_SUCCESS;
}

void URDFRendererPlugin::applyJointTransforms() {
    for (const auto& [link_name, node] : link_nodes_) {
        Ogre::Matrix4 transform = Ogre::Matrix4::IDENTITY;
        computeLinkTransform(link_name, transform);

        Ogre::Vector3 position = transform.getTrans();
        Ogre::Quaternion orientation(transform.linear());

        node->setPosition(position);
        node->setOrientation(orientation);
    }
}

void URDFRendererPlugin::buildJointTree() {
    if (!urdf_loader_.model) return;

    joint_to_child_link_.clear();
    joint_to_parent_link_.clear();
    link_to_parent_joint_.clear();

    for (const auto& [joint_name, joint] : urdf_loader_.model->joints_) {
        joint_to_parent_link_[joint_name] = joint->parent_link_name;
        joint_to_child_link_[joint_name] = joint->child_link_name;
        link_to_parent_joint_[joint->child_link_name] = joint_name;
    }
}

void URDFRendererPlugin::computeLinkTransform(const std::string& link_name, Ogre::Matrix4& transform) {
    if (!urdf_loader_.model) {
        transform = Ogre::Matrix4::IDENTITY;
        return;
    }

    auto parent_joint_it = link_to_parent_joint_.find(link_name);
    if (parent_joint_it == link_to_parent_joint_.end()) {
        transform = Ogre::Matrix4::IDENTITY;
        return;
    }

    const std::string& joint_name = parent_joint_it->second;
    auto joint_it = urdf_loader_.model->joints_.find(joint_name);
    if (joint_it == urdf_loader_.model->joints_.end()) {
        transform = Ogre::Matrix4::IDENTITY;
        return;
    }

    auto joint = joint_it->second;

    Ogre::Matrix4 parent_transform;
    computeLinkTransform(joint->parent_link_name, parent_transform);

    const urdf::Pose& origin = joint->parent_to_joint_origin_transform;
    Ogre::Matrix4 origin_transform;
    origin_transform.makeTransform(
        Ogre::Vector3(origin.position.x, origin.position.y, origin.position.z),
        Ogre::Vector3::UNIT_SCALE,
        Ogre::Quaternion(origin.rotation.w, origin.rotation.x, origin.rotation.y, origin.rotation.z)
    );

    double angle = 0.0;
    auto angle_opt = urdf_loader_.getJointAngle(joint_name);
    if (angle_opt) {
        angle = *angle_opt;
    }

    Ogre::Matrix4 joint_rotation = Ogre::Matrix4::IDENTITY;
    if (joint->type == urdf::Joint::REVOLUTE || joint->type == urdf::Joint::CONTINUOUS) {
        Ogre::Vector3 axis(joint->axis.x, joint->axis.y, joint->axis.z);
        axis.normalise();
        Ogre::Quaternion rot(Ogre::Radian(angle), axis);
        joint_rotation.makeTransform(Ogre::Vector3::ZERO, Ogre::Vector3::UNIT_SCALE, rot);
    } else if (joint->type == urdf::Joint::PRISMATIC) {
        Ogre::Vector3 axis(joint->axis.x, joint->axis.y, joint->axis.z);
        axis.normalise();
        Ogre::Vector3 translation = axis * angle;
        joint_rotation.makeTransform(translation, Ogre::Vector3::UNIT_SCALE, Ogre::Quaternion::IDENTITY);
    }

    transform = parent_transform * origin_transform * joint_rotation;
}

UrdfPluginError URDFRendererPlugin::setCamera(const UrdfCameraConfig& config) {
    if (!initialized_) {
        return URDF_ERROR_INIT_FAILED;
    }

    camera_config_ = config;

    if (camera_) {
        Ogre::SceneNode* camNode = camera_->getParentSceneNode();
        if (camNode) {
            camNode->setPosition(config.position[0], config.position[1], config.position[2]);
            camNode->lookAt(Ogre::Vector3(config.look_at[0], config.look_at[1], config.look_at[2]), Ogre::Node::TS_WORLD);
        }
        camera_->setFOVy(Ogre::Degree(config.fov_degrees));
        camera_->setNearClipDistance(config.near_clip);
        camera_->setFarClipDistance(config.far_clip);
    }

    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::getCamera(UrdfCameraConfig* config) const {
    if (!config) {
        return URDF_ERROR_INVALID_PARAMETER;
    }

    *config = camera_config_;
    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::setRenderConfig(const UrdfRenderConfig& config) {
    if (!initialized_) {
        return URDF_ERROR_INIT_FAILED;
    }

    bool size_changed = (render_config_.width != config.width ||
                        render_config_.height != config.height);

    render_config_ = config;

    if (size_changed) {
        image_width_ = config.width;
        image_height_ = config.height;

        if (render_texture_) {
            render_texture_ = nullptr;
        }
        if (texture_ptr_) {
            Ogre::TextureManager::getSingleton().remove(texture_ptr_);
            texture_ptr_.reset();
        }

        if (!createRenderTexture()) {
            return URDF_ERROR_RENDER_FAILED;
        }
    }

    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::renderFrame() {
    if (!initialized_) {
        return URDF_ERROR_INIT_FAILED;
    }

    if (!model_loaded_) {
        return URDF_ERROR_NO_MODEL_LOADED;
    }

    std::lock_guard<std::mutex> lock(render_mutex_);

    try {
        render_texture_->update();
        captureFrameBuffer();
        return URDF_SUCCESS;
    } catch (const Ogre::Exception& e) {
        setError(std::string("Render failed: ") + e.getDescription());
        return URDF_ERROR_RENDER_FAILED;
    }
}

void URDFRendererPlugin::captureFrameBuffer() {
    const size_t channels = render_config_.transparent_background ? 4 : 3;
    const size_t buffer_size = image_width_ * image_height_ * channels;

    image_buffer_.resize(buffer_size);

    Ogre::PixelFormat pixel_format = render_config_.transparent_background
        ? Ogre::PF_BYTE_RGBA
        : Ogre::PF_BYTE_RGB;

    Ogre::PixelBox pixel_box(image_width_, image_height_, 1, pixel_format, image_buffer_.data());
    render_texture_->copyContentsToMemory(Ogre::Box(0, 0, image_width_, image_height_), pixel_box, Ogre::RenderTarget::FB_AUTO);
}

UrdfPluginError URDFRendererPlugin::startContinuousRender(uint32_t target_fps) {
    if (!initialized_ || !model_loaded_) {
        return URDF_ERROR_INIT_FAILED;
    }

    target_fps_ = target_fps > 0 ? target_fps : 30;
    continuous_render_ = true;

    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::stopContinuousRender() {
    continuous_render_ = false;
    return URDF_SUCCESS;
}

bool URDFRendererPlugin::isRendering() const {
    return continuous_render_;
}

UrdfPluginError URDFRendererPlugin::getImageBuffer(UrdfImageData* image_data) {
    if (!image_data) {
        return URDF_ERROR_INVALID_PARAMETER;
    }

    if (image_buffer_.empty()) {
        setError("No image rendered yet");
        return URDF_ERROR_RENDER_FAILED;
    }

    std::lock_guard<std::mutex> lock(render_mutex_);

    image_data->data = image_buffer_.data();
    image_data->width = image_width_;
    image_data->height = image_height_;
    image_data->channels = render_config_.transparent_background ? 4 : 3;
    image_data->data_size = image_buffer_.size();
    image_data->format = render_config_.transparent_background ? URDF_IMAGE_FORMAT_RGBA : URDF_IMAGE_FORMAT_RGB;

    return URDF_SUCCESS;
}

UrdfPluginError URDFRendererPlugin::saveImage(const std::string& path, UrdfImageFormat format) {
    (void)path;
    (void)format;
    setError("Image saving not yet implemented");
    return URDF_ERROR_NOT_IMPLEMENTED;
}

UrdfPluginError URDFRendererPlugin::copyImageBuffer(uint8_t* buffer, size_t buffer_size, size_t* bytes_written) {
    if (!buffer || !bytes_written) {
        return URDF_ERROR_INVALID_PARAMETER;
    }

    if (image_buffer_.empty()) {
        setError("No image rendered yet");
        return URDF_ERROR_RENDER_FAILED;
    }

    std::lock_guard<std::mutex> lock(render_mutex_);

    const size_t copy_size = std::min(buffer_size, image_buffer_.size());
    std::memcpy(buffer, image_buffer_.data(), copy_size);
    *bytes_written = copy_size;

    return URDF_SUCCESS;
}

cv::Mat URDFRendererPlugin::getImageAsMat() const {
    if (image_buffer_.empty()) {
        throw std::runtime_error("No image available. Call renderFrame() first.");
    }

    std::lock_guard<std::mutex> lock(render_mutex_);

    int height = static_cast<int>(image_height_);
    int width = static_cast<int>(image_width_);
    int channels = render_config_.transparent_background ? 4 : 3;
    int cv_type = (channels == 4) ? CV_8UC4 : CV_8UC3;

    cv::Mat wrapper(height, width, cv_type,
                    const_cast<uint8_t*>(image_buffer_.data()));

    return wrapper.clone();
}

void URDFRendererPlugin::setError(const std::string& error) {
    last_error_ = error;
    std::cerr << "URDFRendererPlugin Error: " << error << std::endl;
}

Ogre::SceneNode* URDFRendererPlugin::findNodeForLink(const std::string& link_name) {
    auto it = link_nodes_.find(link_name);
    return (it != link_nodes_.end()) ? it->second : nullptr;
}

void URDFRendererPlugin::updateSceneFromJoints() {
}

} // namespace drivers
