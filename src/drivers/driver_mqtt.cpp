#include <drivers/driver_mqtt.hpp>
#include <spdlog/spdlog.h>

namespace drivers
{
using InputTopic = MqttClient::InputTopic;
using OutputTopic = MqttClient::OutputTopic;
using TopicMeta = MqttClient::TopicMeta;

const std::array<TopicMeta, static_cast<size_t>(InputTopic::COUNT_INPUT_TOPICS)> MqttClient::INPUT_TOPIC_META_DICT = {
    TopicMeta { "GameStatus", 1 },
    TopicMeta { "GlobalUnitStatus", 1 },
    TopicMeta { "GlobalLogisticsStatus", 1 },
    TopicMeta { "GlobalSpecialMechanism", 1 },
    TopicMeta { "Event", 1 },
    TopicMeta { "RobotInjuryStat", 1 },
    TopicMeta { "RobotRespawnStatus", 1 },
    TopicMeta { "RobotStaticStatus", 1 },
    TopicMeta { "RobotDynamicStatus", 1 },
    TopicMeta { "RobotModuleStatus", 1 },
    TopicMeta { "RobotPosition", 1 },
    TopicMeta { "Buff", 1 },
    TopicMeta { "PenaltyInfo", 1 },
    TopicMeta { "RobotPathPlanInfo", 1 },
    TopicMeta { "RadarInfoToClient", 1 },
    TopicMeta { "CustomByteBlock", 1 },
    TopicMeta { "TechCoreMotionStateSync", 1 },
    TopicMeta { "RobotPerformanceSelectionSync", 1 },
    TopicMeta { "DeployModeStatusSync", 1 },
    TopicMeta { "RuneStatusSync", 1 },
    TopicMeta { "SentinelStatusSync", 1 },
    TopicMeta { "DartSelectTargetStatusSync", 1 },
    TopicMeta { "GuardCtrlResult", 1 },
    TopicMeta { "AirSupportStatusSync", 1 },
};

const std::array<TopicMeta, static_cast<size_t>(OutputTopic::COUNT_OUTPUT_TOPICS)> MqttClient::OUTPUT_TOPIC_META_DICT = {
    TopicMeta { "RemoteControl", 1 },
    TopicMeta { "MapClickInfoNotify", 1 },
    TopicMeta { "AssemblyCommand", 1 },
    TopicMeta { "RobotPerformanceSelectionCommand", 1 },
    TopicMeta { "HeroDeployModelEventCommand", 1 },
    TopicMeta { "RuneActivateCommand", 1 },
    TopicMeta { "DartCommand", 1 },
    TopicMeta { "GuardCtrlCommand", 1 },
    TopicMeta { "AirSupportCommand", 1 },
};

MqttClient::MqttClient(const std::string& ip, int port, const std::string& client_id)
{
    this->ip_ = ip;
    this->port_ = port;
    this->client_id_ = client_id;
    // 创建mqtt客户端对象
    std::string mqtt_addr = "mqtt://" + ip_ + ":" + std::to_string(port_);
    this->client_ = std::make_unique<mqtt::async_client>(mqtt_addr, client_id_);
}

bool MqttClient::Connect()
{
    try
    {
        auto connect_options = mqtt::connect_options_builder::v3()
                                   .clean_session(false)
                                   .automatic_reconnect()
                                   .finalize();
        client_->start_consuming();
        spdlog::info("Connecting to MQTT server...");
        auto token = client_->connect(connect_options);
        auto rsp = token->get_connect_response();
        if (!rsp.is_session_present())
        {
            spdlog::info("No session present on server. Subscribing...");
            InitSubscriber();
        }
        spdlog::info("Connected to MQTT Server at {}:{}", ip_, port_);
        return true;
    }
    catch (const mqtt::exception& e)
    {
        spdlog::error("Failed to connect to MQTT broker at {}:{} - what(): {}", ip_, port_, e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown error connecting to MQTT broker at {}:{}", ip_, port_);
    }
    return false;
}

bool MqttClient::Disconnect()
{
    try
    {
        spdlog::info("Disconnecting from MQTT server...");
        auto token = client_->disconnect();
        token->wait();
        spdlog::info("Disconnected from MQTT server.");
        return true;
    }
    catch (const mqtt::exception& e)
    {
        spdlog::error("Failed to disconnect from MQTT broker - what(): {}", e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown error disconnecting from MQTT broker");
    }
    return false;
}



bool MqttClient::Publish(const std::string& topic, const std::string& payload, int qos)
{
    try
    {
        if (!client_)
        {
            spdlog::error("MQTT client not initialized, cannot publish to {}", topic);
            return false;
        }
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(qos);
        auto tok = client_->publish(msg);
        if (tok)
        {
            tok->wait();
        }
        spdlog::info("Published to MQTT topic: {} (qos={})", topic, qos);
        return true;
    }
    catch (const mqtt::exception& e)
    {
        spdlog::error("Failed to publish to {}: what(): {}", topic, e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown error publishing to {}", topic);
    }
    return false;
}

bool MqttClient::Publish(OutputTopic topic, const std::string& payload, int qos)
{
    const auto& info = GetOutputTopic(topic);
    int effective_qos = (qos >= 0 ? qos : info.qos);
    return Publish(info.name, payload, effective_qos);
}

bool MqttClient::Subscribe(const std::string& topic, int qos)
{
    try
    {
        if (!client_)
        {
            spdlog::error("MQTT client not initialized, cannot subscribe to {}", topic);
            return false;
        }
        auto tok = client_->subscribe(topic, qos);
        if (tok)
        {
            tok->wait();
        }
        spdlog::info("Subscribed to MQTT topic: {} (qos={})", topic, qos);
        return true;
    }
    catch (const mqtt::exception& e)
    {
        spdlog::error("Failed to subscribe to {}: what(): {}", topic, e.what());
    }
    catch (...)
    {
        spdlog::error("Unknown error subscribing to {}", topic);
    }
    return false;
}

bool MqttClient::Subscribe(InputTopic topic, int qos)
{
    const auto& info = GetInputTopic(topic);
    int effective_qos = (qos >= 0 ? qos : info.qos);
    return Subscribe(info.name, effective_qos);
}

void MqttClient::InitSubscriber()
{
    // 订阅所有输入主题
    for (size_t i = 0; i < static_cast<size_t>(InputTopic::COUNT_INPUT_TOPICS); ++i)
    {
        const auto& topic_meta = INPUT_TOPIC_META_DICT[i];
        Subscribe(topic_meta.name, topic_meta.qos);
    }
}


const MqttClient::TopicMeta& MqttClient::GetInputTopic(InputTopic t)
{
    return INPUT_TOPIC_META_DICT[static_cast<size_t>(t)];
}

const MqttClient::TopicMeta& MqttClient::GetOutputTopic(OutputTopic t)
{
    return OUTPUT_TOPIC_META_DICT[static_cast<size_t>(t)];
}


// 例：在 message 回调里你可以把 topic 字符串转换成 enum 再处理
// 简洁示例（实际在 ClientCallback::message_arrived 中调用）
void MqttClient::SetMessageHandler(std::function<void(const std::string& topic, const std::string& payload)> handler)
{
    std::lock_guard lock(handler_mutex_);
    message_handler_ = std::move(handler);
}

// ... protobuf/json 解析和具体处理部分保留在 user handler 中 ...
} // namespace drivers