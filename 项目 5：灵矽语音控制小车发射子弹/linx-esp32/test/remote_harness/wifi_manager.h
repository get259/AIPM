#pragma once
class WifiManager {
public:
    bool connected = true;
    static WifiManager& GetInstance() { static WifiManager wifi; return wifi; }
    bool IsConnected() { return connected; }
};
