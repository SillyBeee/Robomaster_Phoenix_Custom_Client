#ifndef DRIVER_GAMEPAD_HPP
#define DRIVER_GAMEPAD_HPP

#include <SDL.h>
#include <iostream>
#include <memory>
#include <thread>
namespace drivers{


enum class GamePadInput : uint8_t {
    ButtonA = 0,          // 右下角 A（button0）
    ButtonB = 1,          // 右下角 B（button1）
    ButtonX = 2,          // 右上角 X（button2）
    ButtonY = 3,          // 右上角 Y（button3）
    PageUp = 4,           // 左上角分页键（button4）
    Steam = 5,            // 左下角 STEAM 键（button5）
    Settings = 6,         // 右上角设置键（button6）
    LeftShoulder = 7,     // 左肩键（button9）
    RightShoulder = 8,    // 右肩键（button10）
    DPadUp = 9,           // 十字键上（button11）
    DPadDown = 10,       // 十字键下（button12）
    DPadLeft = 11,       // 十字键左（button13）
    DPadRight = 12,      // 十字键右（button14）
    ButtonExtra = 13,    // 右下角...键（button15）
    BackRightUpper = 14, // 右上背键（button16）
    BackLeftUpper = 15,  // 左上背键（button17）
    BackRightLower = 16, // 右下背键（button18）
    BackLeftLower = 17,  // 左下背键（button19）
    LeftStickX = 18,     // 左摇杆 X 轴（-32766 ~ 32766，左负）
    LeftStickY = 19,     // 左摇杆 Y 轴（-32766 ~ 32766，上负）
    RightStickX = 20,    // 右摇杆 X 轴（-32766 ~ 32766，左负）
    RightStickY = 21,    // 右摇杆 Y 轴（-32766 ~ 32766，上负）
    LeftTrigger = 22,      // 左扳机（0 ~ 32766）
    RightTrigger = 23      // 右扳机（0 ~ 32766）
};

class GamePad{
public:
    GamePad() = default;
    ~GamePad();
    bool Init();
    int16_t GetInputState(int input);
    void Shutdown();
    void PrintState(std::ostream &os = std::cout) const;
private:
    void PollLoop();

    std::unique_ptr<SDL_GameController, decltype(&SDL_GameControllerClose)> instance_{nullptr, SDL_GameControllerClose};
    int16_t gamepad_input_state_[24] = {0}; // 存储各输入状态
    std::thread core_thread_;
    bool running_ = false;
    

};
}

#endif // DRIVER_GAMEPAD_HPP