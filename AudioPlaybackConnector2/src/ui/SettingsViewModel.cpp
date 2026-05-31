#include <pch.h>

#include <ui/SettingsViewModel.hpp>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

std::vector<SettingsDeviceViewModel> SettingsViewModel::BuildDeviceItems(SettingsData const& settings) {
    std::vector<SettingsDeviceViewModel> items;
    items.reserve(settings.Devices.size());
    for (auto const& device : settings.Devices) {
        items.push_back({
            .Id = device.Id,
            .DisplayName = device.Name.empty() ? device.Id : device.Name,
            .AutoReconnect = device.AutoReconnect,
        });
    }
    return items;
}
