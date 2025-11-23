#include <app-window.h>
#include "callback_center.hpp"

int main() {
    auto main_window = MainWindow::create();
    auto &callback_factory = main_window->global<Callback_Factory>();

    callback_factory.on_open_url([](slint::SharedString url) {
        callback_open_url(url);
    });

    callback_factory.on_set_resolution([&main_window](slint::SharedString resolution) {
        callback_set_resolution(main_window, resolution);
    });

    callback_factory.on_set_fullscreen([&main_window](bool is_fullscreen) {
        callback_set_fullscreen(main_window, is_fullscreen);
    });

    main_window->run();
    return 0;
}