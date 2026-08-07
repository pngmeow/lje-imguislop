#pragma once
#include <string>

namespace monaco::runtime {

// Brings up the embedded Chromium browser process. Blocks until CefInitialize
// has answered; safe to call from any thread and idempotent.
bool start();

// Tears it back down. All browsers must already be closed.
void stop();

bool running();
std::string error();

} // namespace monaco::runtime