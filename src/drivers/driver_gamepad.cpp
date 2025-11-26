#include <SDL_gamecontroller.h>
#include <drivers/driver_gamepad.hpp>
#include <memory>

namespace drivers{

bool GamePad::Init(){
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }

    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            this->instance_.reset(SDL_GameControllerOpen(i));
            if (this->instance_) {
                std::cout << "Opened controller " << i << ": "
                          << (SDL_GameControllerName(this->instance_.get()) ? SDL_GameControllerName(this->instance_.get()) : "unknown")
                          << "\n";
                break;
            }
        }
    }
    
    if (!this->instance_) {
        std::cerr << "No controller found.\n";
        return false;
    }
    this->running_ = true;
    this->core_thread_ = std::thread(&GamePad::PollLoop, this);
    return true;
}

int16_t GamePad::GetInputState(int input) {
    if (input < 0 || input >= 24) {
        std::cerr << "Invalid input index: " << input << "\n";
        return -1;
    }
    return gamepad_input_state_[input];
}


void GamePad::PollLoop(){
    SDL_Event event;
    auto store_axis = [this](GamePadInput input, int16_t value) {
        auto idx = static_cast<size_t>(input);
        if (idx < std::size(gamepad_input_state_)) {
            gamepad_input_state_[idx] = value;
        }
    };
    auto store_button = [this](GamePadInput input, bool pressed) {
        auto idx = static_cast<size_t>(input);
        if (idx < std::size(gamepad_input_state_)) {
            gamepad_input_state_[idx] = pressed ? 1 : 0;
        }
    };

    while (this->running_) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_CONTROLLERAXISMOTION: {
                switch (event.caxis.axis) {
                case SDL_CONTROLLER_AXIS_LEFTX:
                    store_axis(GamePadInput::LeftStickX, event.caxis.value);
                    break;
                case SDL_CONTROLLER_AXIS_LEFTY:
                    store_axis(GamePadInput::LeftStickY, event.caxis.value);
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTX:
                    store_axis(GamePadInput::RightStickX, event.caxis.value);
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTY:
                    store_axis(GamePadInput::RightStickY, event.caxis.value);
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                    store_axis(GamePadInput::LeftTrigger, event.caxis.value);
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                    store_axis(GamePadInput::RightTrigger, event.caxis.value);
                    break;
                default:
                    break;
                }
                break;
            }
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                bool pressed = (event.type == SDL_CONTROLLERBUTTONDOWN);
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:             store_button(GamePadInput::ButtonA, pressed); break;
                case SDL_CONTROLLER_BUTTON_B:             store_button(GamePadInput::ButtonB, pressed); break;
                case SDL_CONTROLLER_BUTTON_X:             store_button(GamePadInput::ButtonX, pressed); break;
                case SDL_CONTROLLER_BUTTON_Y:             store_button(GamePadInput::ButtonY, pressed); break;
                case SDL_CONTROLLER_BUTTON_BACK:      store_button(GamePadInput::PageUp, pressed); break;
                case SDL_CONTROLLER_BUTTON_GUIDE:       store_button(GamePadInput::Steam, pressed); break;
                case SDL_CONTROLLER_BUTTON_START:      store_button(GamePadInput::Settings, pressed); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  store_button(GamePadInput::LeftShoulder, pressed); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: store_button(GamePadInput::RightShoulder, pressed); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:       store_button(GamePadInput::DPadUp, pressed); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     store_button(GamePadInput::DPadDown, pressed); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     store_button(GamePadInput::DPadLeft, pressed); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    store_button(GamePadInput::DPadRight, pressed); break;
                case SDL_CONTROLLER_BUTTON_MISC1:          store_button(GamePadInput::ButtonExtra, pressed); break;
                case SDL_CONTROLLER_BUTTON_PADDLE1:         store_button(GamePadInput::BackRightUpper, pressed); break;
                case SDL_CONTROLLER_BUTTON_PADDLE2:         store_button(GamePadInput::BackLeftUpper, pressed); break;
                case SDL_CONTROLLER_BUTTON_PADDLE3:     store_button(GamePadInput::BackRightLower, pressed); break;
                case SDL_CONTROLLER_BUTTON_PADDLE4:     store_button(GamePadInput::BackLeftLower, pressed); break;
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

void GamePad::Shutdown() {
    this->running_ = false;
    if (this->core_thread_.joinable()) {
        this->core_thread_.join();
    }
    if (this->instance_) {
        SDL_GameControllerClose(this->instance_.release());
    }
}

void GamePad::PrintState(std::ostream &os) const {
    for (size_t i = 0; i < std::size(gamepad_input_state_); ++i) {
        os << gamepad_input_state_[i];
        if (i + 1 < std::size(gamepad_input_state_)) os << ' ';
    }
    os << '\n';
}

GamePad::~GamePad(){
    Shutdown();
}
}