#pragma once

#include <core/Settings.hpp>

#include <string>
#include <vector>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Settings View Model ///////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

struct SettingsDeviceViewModel {
    std::wstring Id;
    std::wstring DisplayName;
    bool AutoReconnect = false;
};

class SettingsViewModel {
public:
    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    [[nodiscard]] static std::vector<SettingsDeviceViewModel> BuildDeviceItems(SettingsData const& settings);
};
