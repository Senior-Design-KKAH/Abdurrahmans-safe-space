#pragma once
#include <RPWProtocol.h>
void initializeApplication();
void serviceApplication();
// Called only by the radio task, before ARQ commit/ACK. Never waits for network.
bool queueUpload(const rpw::Packet& packet);
