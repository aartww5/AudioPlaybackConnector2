#include <pch.h>

#include <services/SettingsController.hpp>

#include <core/DeviceManager.hpp>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

SettingsController::SettingsController(Settings& settings, std::weak_ptr<DeviceManager> deviceManager)
    : m_settings(&settings), m_deviceManager(std::move(deviceManager)) {}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

SettingsData SettingsController::Snapshot() const {
    if (!m_settings) return {};
    auto locked = m_settings->LockSharedData();
    return *locked;
}

void SettingsController::SetGlobalAutoReconnect(bool enabled) {
    if (!m_settings) return;

    std::vector<DeviceSettings> devices;
    {
        auto locked = m_settings->LockExclusiveData();
        locked->GlobalAutoReconnect = enabled;
        devices = locked->Devices;
    }

    if (auto manager = m_deviceManager.lock()) {
        for (const auto& connection : manager->GetConnectedDevices()) {
            bool autoReconnect = enabled;
            for (const auto& device : devices) {
                if (device.Id == connection.Device.Id()) {
                    autoReconnect = autoReconnect || device.AutoReconnect;
                    break;
                }
            }
            manager->SetAutoReconnect(connection.Device.Id(), autoReconnect);
        }
    }

    m_settings->Save(GetModuleHandleW(nullptr));
}

void SettingsController::SetStartWithWindows(bool enabled) {
    if (!m_settings) return;
    {
        auto locked = m_settings->LockExclusiveData();
        locked->StartWithWindows = enabled;
    }
    m_settings->Save(GetModuleHandleW(nullptr));
}

void SettingsController::SetDeviceAutoReconnect(std::wstring const& deviceId, bool enabled) {
    if (!m_settings) return;

    bool globalAutoReconnect = false;
    {
        auto locked = m_settings->LockExclusiveData();
        for (auto& device : locked->Devices) {
            if (device.Id == deviceId) {
                device.AutoReconnect = enabled;
                break;
            }
        }
        globalAutoReconnect = locked->GlobalAutoReconnect;
    }

    if (auto manager = m_deviceManager.lock()) {
        manager->SetAutoReconnect(winrt::hstring(deviceId), globalAutoReconnect || enabled);
    }

    m_settings->Save(GetModuleHandleW(nullptr));
}

void SettingsController::ForgetDevice(std::wstring const& deviceId) {
    if (!m_settings) return;
    {
        auto locked = m_settings->LockExclusiveData();
        std::erase_if(locked->Devices, [&](auto const& device) { return device.Id == deviceId; });
    }
    m_settings->Save(GetModuleHandleW(nullptr));
}
