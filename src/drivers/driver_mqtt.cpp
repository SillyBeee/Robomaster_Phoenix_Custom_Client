#include <drivers/driver_mqtt.hpp>
#include "protocol/protocol.pb.h"
#include <google/protobuf/util/json_util.h> 
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
    TopicMeta { "RaderInfoToClient", 1 },
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
        //设置回调
        this->client_cb_ = std::make_unique<ClientCallback>(std::bind(&MqttClient::MessageCallback, this, std::placeholders::_1));
        client_->set_callback(*client_cb_);

        client_->start_consuming();
        LOG_INFO("Connecting to MQTT server...");
        auto token = client_->connect(connect_options);
        auto rsp = token->get_connect_response();
        if (!rsp.is_session_present())
        {
            LOG_INFO("No session present on server. Subscribing...");
            InitSubscriber();
        }
        LOG_INFO("Connected to MQTT Server at {}:{}", ip_, port_);
        return true;
    }
    catch (const mqtt::exception& e)
    {
        LOG_ERROR("Failed to connect to MQTT broker at {}:{} - what(): {}", ip_, port_, e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error connecting to MQTT broker at {}:{}", ip_, port_);
    }
    return false;
}

bool MqttClient::Disconnect()
{
    try
    {
        LOG_INFO("Disconnecting from MQTT server...");
        auto token = client_->disconnect();
        token->wait();
        LOG_INFO("Disconnected from MQTT server.");
        return true;
    }
    catch (const mqtt::exception& e)
    {
        LOG_ERROR("Failed to disconnect from MQTT broker - what(): {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error disconnecting from MQTT broker");
    }
    return false;
}

bool MqttClient::Publish(const std::string& topic, const std::string& payload, int qos)
{
    try
    {
        if (!client_)
        {
            LOG_ERROR("MQTT client not initialized, cannot publish to {}", topic);
            return false;
        }
        auto msg = mqtt::make_message(topic, payload);
        msg->set_qos(qos);
        auto tok = client_->publish(msg);
        if (tok)
        {
            tok->wait();
        }
        LOG_INFO("Published to MQTT topic: {} (qos={})", topic, qos);
        return true;
    }
    catch (const mqtt::exception& e)
    {
        LOG_ERROR("Failed to publish to {}: what(): {}", topic, e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error publishing to {}", topic);
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
            LOG_ERROR("MQTT client not initialized, cannot subscribe to {}", topic);
            return false;
        }
        auto tok = client_->subscribe(topic, qos);
        if (tok)
        {
            tok->wait();
        }
        LOG_INFO("Subscribed to MQTT topic: {} (qos={})", topic, qos);
        return true;
    }
    catch (const mqtt::exception& e)
    {
        LOG_ERROR("Failed to subscribe to {}: what(): {}", topic, e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error subscribing to {}", topic);
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

void MqttClient::MessageCallback(mqtt::const_message_ptr msg)
{
    if (!msg){return;} 

    // 解析消息并调用用户处理函数
    std::string topic = msg->get_topic();
    std::string payload = msg->to_string();
    LOG_INFO("Received MQTT message on topic: {}", topic);

    if(topic == GetInputTopic(InputTopic::GAME_STATUS).name){
        GameStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("GameStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse GameStatus message");
        }
    }

    else if (GetInputTopic(InputTopic::GLOBAL_UNIT_STATUS).name == topic){
        GlobalUnitStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("GlobalUnitStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse GlobalUnitStatus message");
        }
    }

    else if (GetInputTopic(InputTopic::GLOBAL_LOGISTICS_STATUS).name == topic){
        GlobalLogisticsStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("GlobalLogisticsStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse GlobalLogisticsStatus message");
        }
    }

    else if (GetInputTopic(InputTopic::GLOBAL_SPECIAL_MECHANISM).name == topic){
        GlobalSpecialMechanism status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("GlobalSpecialMechanism JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse GlobalSpecialMechanism message");
        }
    }

    else if (GetInputTopic(InputTopic::EVENT).name == topic){
        Event status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("Event JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse Event message");
        }
    }

    else if (GetInputTopic(InputTopic::ROBOT_INJURY_STAT).name == topic){
        RobotInjuryStat status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotInjuryStat JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotInjuryStat message");
        }
    }

    else if (GetInputTopic(InputTopic::ROBOT_RESPAWN_STATUS).name == topic){
        RobotRespawnStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotRespawnStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotRespawnStatus message");
        }
    }

    else if (GetInputTopic(InputTopic::ROBOT_STATIC_STATUS).name == topic){
        RobotStaticStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotStaticStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotStaticStatus message");
        }
    }
    else if (GetInputTopic(InputTopic::ROBOT_DYNAMIC_STATUS).name == topic){
        RobotDynamicStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotDynamicStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotDynamicStatus message");
        }
    }

    else if (GetInputTopic(InputTopic::ROBOT_MODULE_STATUS).name == topic){
        RobotModuleStatus status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotModuleStatus JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotModuleStatus message");
        }
    }

    else if(GetInputTopic(InputTopic::ROBOT_POSITION).name == topic){
        RobotPosition status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotPosition JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotPosition message");
        }
    }

    else if (GetInputTopic(InputTopic::BUFF).name == topic){
        Buff status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("Buff JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse Buff message");
        }
    }

    else if(GetInputTopic(InputTopic::PENALTY_INFO).name == topic){
        PenaltyInfo status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("PenaltyInfo JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse PenaltyInfo message");
        }
    }

    else if (GetInputTopic(InputTopic::ROBOT_PATH_PLAN_INFO).name == topic){
        RobotPathPlanInfo status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotPathPlanInfo JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotPathPlanInfo message");
        }
    }

    else if (GetInputTopic(InputTopic::RADER_INFO_TO_CLIENT).name == topic){
        RaderInfoToClient status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RadarInfoToClient JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RadarInfoToClient message");
        }
    }

    else if (GetInputTopic(InputTopic::CUSTOM_BYTE_BLOCK).name == topic){
        CustomByteBlock status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("CustomByteBlock JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse CustomByteBlock message");
        }
    }

    else if(GetInputTopic(InputTopic::TECH_CORE_MOTION_STATE_SYNC).name == topic){
        TechCoreMotionStateSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("TechCoreMotionStateSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse TechCoreMotionStateSync message");
        }
    }

    else if (GetInputTopic(InputTopic::ROBOT_PERFORMANCE_SELECTION_SYNC).name == topic){
        RobotPerformanceSelectionSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RobotPerformanceSelectionSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RobotPerformanceSelectionSync message");
        }
    }

    else if (GetInputTopic(InputTopic::DEPLOY_MODE_STATUS_SYNC).name == topic){
        DeployModeStatusSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("DeployModeStatusSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse DeployModeStatusSync message");
        }
    }

    else if( GetInputTopic(InputTopic::RUNE_STATUS_SYNC).name == topic){
        RuneStatusSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("RuneStatusSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse RuneStatusSync message");
        }
    }

    else if (GetInputTopic(InputTopic::SENTINEL_STATUS_SYNC).name == topic){
        SentinelStatusSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("SentinelStatusSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse SentinelStatusSync message");
        }
    }

    else if (GetInputTopic(InputTopic::DART_SELECT_TARGET_STATUS_SYNC).name == topic){
        DartSelectTargetStatusSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("DartSelectTargetStatusSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse DartSelectTargetStatusSync message");
        }
    }

    else if (GetInputTopic(InputTopic::GUARD_CTRL_RESULT).name == topic){
        GuardCtrlResult status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("GuardCtrlResult JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse GuardCtrlResult message");
        }
    }

    else if (GetInputTopic(InputTopic::AIR_SUPPORT_STATUS_SYNC).name == topic){
        AirSupportStatusSync status;
        if (status.ParseFromString(payload)) {
            std::string json;
            google::protobuf::util::MessageToJsonString(status, &json);
            LOG_INFO("AirSupportStatusSync JSON: {}", json);
        } else {
            LOG_ERROR("Failed to parse AirSupportStatusSync message");
        }
    }

    else if(topic.empty()){
        LOG_WARN("Received MQTT message with empty topic");
    }
    else{
        LOG_WARN("Received MQTT message on unknown topic: {}", topic);
    }

}


} // namespace drivers`