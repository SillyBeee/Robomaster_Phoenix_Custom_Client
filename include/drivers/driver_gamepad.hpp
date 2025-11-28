#ifndef DRIVER_GAMEPAD_HPP
#define DRIVER_GAMEPAD_HPP

#include <SDL.h>
#include <iostream>
#include <memory>
#include <thread>
namespace drivers
{

class GamePad
{
public:
    GamePad() = default;
    ~GamePad();
    bool Init();
    int16_t GetInputState(int input);
    void Shutdown();
    void PrintState(std::ostream& os = std::cout) const;
    enum class GamePadInput : uint8_t
    {
        BUTTON_A = 0,
        BUTTON_B = 1,
        BUTTON_X = 2,
        BUTTON_Y = 3,
        PAGE_UP = 4,
        STEAM = 5,
        SETTINGS = 6,
        LEFT_SHOULDER = 7,
        RIGHT_SHOULDER = 8,
        DPAD_UP = 9,
        DPAD_DOWN = 10,
        DPAD_LEFT = 11,
        DPAD_RIGHT = 12,
        BUTTON_EXTRA = 13,
        BACK_RIGHT_UPPER = 14,
        BACK_LEFT_UPPER = 15,
        BACK_RIGHT_LOWER = 16,
        BACK_LEFT_LOWER = 17,
        LEFT_STICK_X = 18,
        LEFT_STICK_Y = 19,
        RIGHT_STICK_X = 20,
        RIGHT_STICK_Y = 21,
        LEFT_TRIGGER = 22,
        RIGHT_TRIGGER = 23
    };

private:
    void PollLoop();

    std::unique_ptr<SDL_GameController, decltype(&SDL_GameControllerClose)>
        instance_ { nullptr, SDL_GameControllerClose };
    int16_t gamepad_input_state_[24] = { 0 }; // 存储各输入状态
    std::thread core_thread_;
    bool running_ = false;
};
} // namespace drivers

#endif // DRIVER_GAMEPAD_HPP