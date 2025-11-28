#ifndef DRIVER_MQTT_HPP
#define DRIVER_MQTT_HPP
#include <mqtt/async_client.h>
namespace drivers
{

class MqttClient
{
public:
    explicit MqttClient(std::string _ip = "192.168.12.1", int _port = 3333, std::string _client_id = "RM_Client");
    ~MqttClient() = default;
    bool Connect();
    bool Disconnect();
    bool PublishMessage(const std::string& topic, const std::string& payload, int qos = 1);
    void ReceiveMessage(mqtt::event& ev);
    void SubscribeTopic(const std::string& topic, int qos = 1);

private:
    void InitSubscriber();
    std::unique_ptr<mqtt::async_client> client_;
    std::thread core_thread_;
    std::string ip_;
    int port_;
    std::string client_id_;
};

}; // namespace drivers
#endif // DRIVER_MQTT_HPP