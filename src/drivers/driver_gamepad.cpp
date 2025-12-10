#include "logger.hpp"
#include <SDL_gamecontroller.h>
#include <drivers/driver_gamepad.hpp>
#include <memory>

namespace drivers
{

bool GamePad::Init()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        LOG_ERROR("SDL_Init failed: {}", SDL_GetError());
        return false;
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i)
    {
        if (SDL_IsGameController(i))
        {
            this->instance_.reset(SDL_GameControllerOpen(i));
            LOG_INFO("Opened controller {}: {}", i, SDL_GameControllerName(this->instance_.get()) ? SDL_GameControllerName(this->instance_.get()) : "unknown");
        }
    }

    if (!this->instance_)
    {
        LOG_WARN("No controller found.");
        return false;
    }
    this->running_ = true;
    this->core_thread_ = std::thread(&GamePad::PollLoop, this);
    return true;
}

int16_t GamePad::GetInputState(int input)
{
    if (input < 0 || input >= 24)
    {
        std::cerr << "Invalid input index: " << input << "\n";
        return -1;
    }
    return gamepad_input_state_[input];
}

void GamePad::PollLoop()
{
    SDL_Event event;
    auto store_axis = [this](GamePadInput input, int16_t value)
    {
        auto idx = static_cast<size_t>(input);
        if (idx < std::size(gamepad_input_state_))
        {
            gamepad_input_state_[idx] = value;
        }
    };
    auto store_button = [this](GamePadInput input, bool pressed)
    {
        auto idx = static_cast<size_t>(input);
        if (idx < std::size(gamepad_input_state_))
        {
            gamepad_input_state_[idx] = pressed ? 1 : 0;
        }
    };

    while (this->running_)
    {
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_CONTROLLERAXISMOTION:
                {
                    switch (event.caxis.axis)
                    {
                        case SDL_CONTROLLER_AXIS_LEFTX:
                            store_axis(GamePadInput::LEFT_STICK_X, event.caxis.value);
                            break;
                        case SDL_CONTROLLER_AXIS_LEFTY:
                            store_axis(GamePadInput::LEFT_STICK_Y, event.caxis.value);
                            break;
                        case SDL_CONTROLLER_AXIS_RIGHTX:
                            store_axis(GamePadInput::RIGHT_STICK_X, event.caxis.value);
                            break;
                        case SDL_CONTROLLER_AXIS_RIGHTY:
                            store_axis(GamePadInput::RIGHT_STICK_Y, event.caxis.value);
                            break;
                        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                            store_axis(GamePadInput::LEFT_TRIGGER, event.caxis.value);
                            break;
                        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                            store_axis(GamePadInput::RIGHT_TRIGGER, event.caxis.value);
                            break;
                        default:
                            break;
                    }
                    break;
                }
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                {
                    bool pressed = (event.type == SDL_CONTROLLERBUTTONDOWN);
                    switch (event.cbutton.button)
                    {
                        case SDL_CONTROLLER_BUTTON_A:
                            store_button(GamePadInput::BUTTON_A, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_B:
                            store_button(GamePadInput::BUTTON_B, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_X:
                            store_button(GamePadInput::BUTTON_X, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_Y:
                            store_button(GamePadInput::BUTTON_Y, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_BACK:
                            store_button(GamePadInput::PAGE_UP, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_GUIDE:
                            store_button(GamePadInput::STEAM, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_START:
                            store_button(GamePadInput::SETTINGS, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                            store_button(GamePadInput::LEFT_SHOULDER, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                            store_button(GamePadInput::RIGHT_SHOULDER, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            store_button(GamePadInput::DPAD_UP, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            store_button(GamePadInput::DPAD_DOWN, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                            store_button(GamePadInput::DPAD_LEFT, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                            store_button(GamePadInput::DPAD_RIGHT, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_MISC1:
                            store_button(GamePadInput::BUTTON_EXTRA, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_PADDLE1:
                            store_button(GamePadInput::BACK_RIGHT_UPPER, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_PADDLE2:
                            store_button(GamePadInput::BACK_LEFT_UPPER, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_PADDLE3:
                            store_button(GamePadInput::BACK_RIGHT_LOWER, pressed);
                            break;
                        case SDL_CONTROLLER_BUTTON_PADDLE4:
                            store_button(GamePadInput::BACK_LEFT_LOWER, pressed);
                            break;
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        SDL_Delay(10);
    }
}

void GamePad::Shutdown()
{
    this->running_ = false;
    if (this->core_thread_.joinable())
    {
        this->core_thread_.join();
    }
    if (this->instance_)
    {
        SDL_GameControllerClose(this->instance_.release());
    }
}

void GamePad::PrintState(std::ostream& os) const
{
    for (size_t i = 0; i < std::size(gamepad_input_state_); ++i)
    {
        os << gamepad_input_state_[i];
        if (i + 1 < std::size(gamepad_input_state_))
            os << ' ';
    }
    os << '\n';
}

GamePad::~GamePad()
{
    Shutdown();
}
} // namespace drivers