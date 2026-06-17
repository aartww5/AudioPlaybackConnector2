#pragma once

#include <core/Settings.hpp>

class DeviceManager;

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Settings Controller ///////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

class ISettingsController {
public:
    virtual ~ISettingsController() = default;

    virtual SettingsData Snapshot() const = 0;
    virtual void SetGlobalAutoReconnect(bool enabled) = 0;
    virtual void SetStartWithWindows(bool enabled) = 0;
    virtual void SetShowNotifications(bool enabled) = 0;
    virtual bool SetSettingsWindowBounds(PersistedWindowBounds bounds) = 0;
    virtual bool ClearSettingsWindowBounds() = 0;
    virtual void Save() = 0;
    virtual void SetDeviceAutoReconnect(std::wstring const& deviceId, bool enabled) = 0;
    virtual void ForgetDevice(std::wstring const& deviceId) = 0;
};

class SettingsController final : public ISettingsController {
public:
    SettingsController(std::shared_ptr<Settings> settings, std::weak_ptr<DeviceManager> deviceManager);

    SettingsData Snapshot() const override;
    void SetGlobalAutoReconnect(bool enabled) override;
    void SetStartWithWindows(bool enabled) override;
    void SetShowNotifications(bool enabled) override;
    bool SetSettingsWindowBounds(PersistedWindowBounds bounds) override;
    bool ClearSettingsWindowBounds() override;
    void Save() override;
    void SetDeviceAutoReconnect(std::wstring const& deviceId, bool enabled) override;
    void ForgetDevice(std::wstring const& deviceId) override;

private:
    std::shared_ptr<Settings> m_settings;
    std::weak_ptr<DeviceManager> m_deviceManager;
};
