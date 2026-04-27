#ifndef COMPONENT_MANAGER_HPP
#define COMPONENT_MANAGER_HPP


#include "protocol.pb.h"
#include <app-window.h>
#include <slint_color.h>
#include <string>

class ComponentManager {
public:
    static ComponentManager& GetInstance();

    ComponentManager(const ComponentManager&) = delete;
    ComponentManager& operator=(const ComponentManager&) = delete;

    // 初始化函数，传入 factory
    void Init(const Callback_Factory& factory);

    void LoadSettings(const std::string& config_path = "");
    void LoadComponents(std::string config_path = "");
    void SaveComponents(const std::string& config_path = "");

    void SetGameStatus(GameStatus input);
    void SetGlobalUnitStatus(GlobalUnitStatus input);
    void SetGlobalLogisticsStatus(GlobalLogisticsStatus input);
    void SetRobotRespawnStatus(RobotRespawnStatus input);
    void SetRobotStaticStatus(RobotStaticStatus input);
    void SetRobotDynamicStatus(RobotDynamicStatus input);
    void SetRobotModuleStatus(RobotModuleStatus input);
    void SetRuneStatus(RuneStatusSync input);
    void SetSentryStatus(SentryStatusSync input);
    void SetDartSelectTargetStatus(DartSelectTargetStatusSync input);
    void SetSentryCtrlResult(SentryCtrlResult input);

private:
    ComponentManager() = default;
    ~ComponentManager() = default;


    static slint::Color HexToColor(const std::string &hex);

private:

    const Callback_Factory* factory_ptr_ = nullptr;
};


#define COMPONENT_MANAGER ComponentManager::GetInstance()

#endif // COMPONENT_MANAGER_HPP
