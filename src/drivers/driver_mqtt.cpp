#include <drivers/driver_mqtt.hpp>
#include <spdlog/spdlog.h>
namespace drivers
{

MqttClient::MqttClient(std::string _ip, int _port, std::string _client_id)
{
    this->ip_ = _ip;
    this->port_ = _port;
    this->client_id_ = _client_id;
    // 创建mqtt客户端对象
    std::string mqtt_addr = "mqtt://" + ip_ + ":" + std::to_string(port_);
    this->client_ = std::make_unique<mqtt::async_client>(mqtt_addr, client_id_);
    auto connect_options =
        mqtt::connect_options_builder().clean_session().finalize();
}

bool MqttClient::Connect()
{
    try
    {
        this->client_->start_consuming();
        spdlog::info("Connecting to MQTT Server...");
        auto token = this->client_->connect();
        auto rsp = token->get_connect_response();
        if (!rsp.is_session_present())
        {
            spdlog::info("No session present on server. Subscribing...");
            InitSubscriber();
        }
    }
    catch (const mqtt::exception& e)
    {
        spdlog::error("MQTT Connect failed: {}", e.what());
        return false;
    }
    return true;
}

void MqttClient::InitSubscriber()
{
    // 按文档列出的主题及 QoS 列表（每行注释说明话题含义及频率）
    const std::vector<std::pair<std::string, int>> topics = {
        { "GameStatus", 1 },                    // 同步比赛全局状态信息 5Hz
        { "GlobalUnitStatus", 1 },              // 同步基地、前哨站和所有机器人状态1Hz
        { "GlobalLogisticsStatus", 1 },         // 同步全局后勤信息 1Hz
        { "GlobalSpecialMechanism", 1 },        // 同步正在生效的全局特殊机制 1Hz
        { "Event", 1 },                         // 全局事件通知 触发时发送
        { "RobotInjuryStat", 1 },               // 机器人一次存活期间累计受伤统计1Hz
        { "RobotRespawnStatus", 1 },            // 机器人复活状态同步 1Hz
        { "RobotStaticStatus", 1 },             // 机器人固定属性和配置 1Hz（或配置变更时）
        { "RobotDynamicStatus", 1 },            // 机器人实时数据 10Hz
        { "RobotModuleStatus", 1 },             // 机器人各模块运行状态 1Hz
        { "RobotPosition", 1 },                 //机器人空间坐标和朝向 1Hz
        { "Buff", 1 },                          //Buff效果信息 获得增益时触发发送，此后1Hz 定频发送直到失去增益
        { "PenaltyInfo", 1 },                   //判罚信息同步 触发式发送
        { "RobotPathPlanInfo", 1 },             //哨兵轨迹规划信息 1Hz
        { "RadarInfoToClient", 1 },             //雷达发送的机器人位置信息 1Hz
        { "CustomByteBlock", 1 },               //自定义字节块数据 50Hz
        { "TechCoreMotionStateSync", 1 },       //科技核心运动状态同步 1Hz
        { "RobotPerformanceSelectionSync", 1 }, //步兵/英雄性能体系状态同步 1Hz
        { "DeployModeStatusSync", 1 },          //英雄部署模式状态同步 1Hz
        { "RuneStatusSync", 1 },                  //能量机关状态同步 1Hz
        { "SentinelStatusSync", 1 },                //哨兵状态同步 1Hz
        { "DartSelectTargetStatusSync", 1 },         //飞镖目标选择状态同步 1Hz
        { "GuardCtrlResult", 1 },                      //哨兵控制指令结果反馈 1Hz
        { "AirSupportStatusSync", 1 }                   //空中支援状态同步 1Hz
    };

    for (const auto& t: topics)
    {
        try
        {
            if (!this->client_)
            {
                spdlog::error("MQTT client not initialized, cannot subscribe to {}", t.first);
                continue;
            }
            // 同步等待订阅完成，便于检查错误
            auto tok = this->client_->subscribe(t.first, t.second);
            if (tok){
                tok->wait();
            }
            spdlog::info("Subscribed to MQTT topic: {} (qos={})", t.first, t.second);
        }
        catch (const mqtt::exception& e)
        {
            spdlog::error("Failed to subscribe to {}: what(): {}", t.first, e.what());
        }
        catch (const std::exception& e)
        {
            spdlog::error("Failed to subscribe to {}: {}", t.first, e.what());
        }
        catch (...)
        {
            spdlog::error("Unknown error subscribing to {}", t.first);
        }
    }
}
} // namespace drivers