// Wi-Fi station that keeps reconnecting, used only for the time of day.
#pragma once

// Starts Wi-Fi if ssid is not empty and returns whether it did. Initialises the network stack in
// either case, which time sync needs.
bool start_network(const char* ssid, const char* password);
