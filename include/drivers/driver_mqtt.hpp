#ifndef DRIVER_MQTT_HPP
#define DRIVER_MQTT_HPP
#include <array>
#include <functional>
#include <mqtt/async_client.h>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace drivers
{

class MqttClient
{
public:
    enum class InputTopic : uint8_t
    {
        // input topics enum
        GAME_STATUS = 0,                  // 比赛全局状态信息 — 5Hz
        GLOBAL_UNIT_STATUS,               // 基地/前哨/所有机器人状态 — 1Hz
        GLOBAL_LOGISTICS_STATUS,          // 全局后勤信息 — 1Hz
        GLOBAL_SPECIAL_MECHANISM,         // 正在生效的全局特殊机制 — 1Hz
        EVENT,                            // 全局事件通知 — 触发式发送
        ROBOT_INJURY_STAT,                // 机器人一次存活期间累计受伤统计 — 1Hz
        ROBOT_RESPAWN_STATUS,             // 机器人复活状态同步 — 1Hz
        ROBOT_STATIC_STATUS,              // 机器人固定属性与配置 — 1Hz
        ROBOT_DYNAMIC_STATUS,             // 机器人实时数据（姿态/行为等） — 10Hz
        ROBOT_MODULE_STATUS,              // 机器人各模块运行状态 — 1Hz
        ROBOT_POSITION,                   // 机器人空间坐标与朝向 — 1Hz
        BUFF,                             // Buff 效果通知 — 触发后持续发送（1Hz）
        PENALTY_INFO,                     // 判罚信息同步 — 触发式发送
        ROBOT_PATH_PLAN_INFO,             // 机器人路径/规划信息 — 1Hz
        RADER_INFO_TO_CLIENT,             // 雷达发送的机器人位置信息 — 1Hz
        CUSTOM_BYTE_BLOCK,                // 自定义字节块（高频数据） — 50Hz
        TECH_CORE_MOTION_STATE_SYNC,      // 科技核心运动状态同步 — 1Hz
        ROBOT_PERFORMANCE_SELECTION_SYNC, // 性能体系状态同步 — 1Hz
        DEPLOY_MODE_STATUS_SYNC,          // 部署模式状态同步 — 1Hz
        RUNE_STATUS_SYNC,                 // 能量机关/符文状态同步 — 1Hz
        SENTINEL_STATUS_SYNC,             // 哨兵状态同步 — 1Hz
        DART_SELECT_TARGET_STATUS_SYNC,   // 飞镖目标选择状态同步 — 1Hz
        GUARD_CTRL_RESULT,                // 哨兵控制指令结果反馈 — 1Hz
        AIR_SUPPORT_STATUS_SYNC,          // 空中支援状态同步 — 1Hz
        COUNT_INPUT_TOPICS,
    };

    enum class OutputTopic : uint8_t
    {
        // output topics enum
        REMOTE_CONTROL = 0,                  //传输鼠标键盘输入和自定义数据 75hz
        MAP_CLICK_INFO_NOTIFY,               //云台手地图点击标记 触发式发送
        ASSEMBLY_COMMAND,                    //工程装配指令 1hz
        ROBOT_PERFORMANCE_SELECTION_COMMAND, //步兵/英雄选择性能体系 1hz
        HERO_DEPLOY_MODEL_EVENT_COMMAND,     //英雄部署模式相关指令 1hz
        RUNE_ACTIVATE_COMMAND,               //能量机关激活指令 1hz
        DART_COMMAND,                        //飞镖控制指令 1hz
        GUARD_CTRL_COMMAND,                  //哨兵控制指令 1hz
        AIR_SUPPORT_COMMAND,                 //空中支援指令 1hz
        COUNT_OUTPUT_TOPICS,
    };

    struct TopicMeta
    {
        std::string name;
        int qos;
    };

    explicit MqttClient(const std::string& ip = "192.168.12.1", int port = 3333, const std::string& client_id = "RM_Client");

    bool Connect();
    bool Disconnect();
    // void StartProcessing();
    // void StopProcessing();

    //支持通过enum与string两种方式发布和订阅
    bool Publish(const std::string& topic, const std::string& payload, int qos = 1);
    bool Publish(OutputTopic topic, const std::string& payload, int qos = -1);
    bool Subscribe(const std::string& topic, int qos = 1);
    bool Subscribe(InputTopic topic, int qos = -1);

    // 通过enum获取topic string 与
    static const TopicMeta& GetInputTopic(InputTopic t);
    static const TopicMeta& GetOutputTopic(OutputTopic t);


    ~MqttClient() = default;

private:
    void InitSubscriber();
    void MessageCallback(mqtt::const_message_ptr msg);

    std::unique_ptr<mqtt::async_client> client_;
    std::unique_ptr<mqtt::callback> client_cb_;

    std::thread core_thread_;
    std::string ip_;
    int port_;
    std::string client_id_;

    std::mutex handler_mutex_;

    //enum与string&qos对应表
    static const std::array<TopicMeta, static_cast<size_t>(InputTopic::COUNT_INPUT_TOPICS)> INPUT_TOPIC_META_DICT;
    static const std::array<TopicMeta, static_cast<size_t>(OutputTopic::COUNT_OUTPUT_TOPICS)> OUTPUT_TOPIC_META_DICT;
};


// MQTT 客户端回调类
class ClientCallback : public virtual mqtt::callback
{
public:
    using MsgFn = std::function<void(mqtt::const_message_ptr)>;
    explicit ClientCallback(MsgFn fn) : fn_(std::move(fn)) {}
    void connection_lost(const std::string& cause) override
    {
        spdlog::warn("MQTT connection lost: {}", cause);
    }
    void message_arrived(mqtt::const_message_ptr msg) override
    {
        if (fn_){
            fn_(msg);
        }  
    }
    void delivery_complete(mqtt::delivery_token_ptr token) override
    {
        (void)token;
    }

private:
    MsgFn fn_;
};


} // namespace drivers
#endif // DRIVER_MQTT_HPP