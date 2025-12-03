#ifndef CALLBACK_CENTER_HPP
#define CALLBACK_CENTER_HPP
#include "slint_string.h"
#include <app-window.h>

void callback_open_url(slint::SharedString url);

void callback_set_resolution(slint::ComponentHandle<MainWindow>& window, slint::SharedString resolution);

void callback_set_fullscreen(slint::ComponentHandle<MainWindow>& window, bool is_fullscreen);

#endif // CALLBACK_CENTER_HPP