#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Device Connection Info ////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

struct DeviceConnectionInfo {
    winrt::Windows::Devices::Enumeration::DeviceInformation Device{nullptr};
    winrt::Windows::Media::Audio::AudioPlaybackConnection Connection{nullptr};
    winrt::event_token StateChangedToken{};
    bool AutoReconnect = false;
    bool IsOpen = false;
};

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Device Session Store //////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

class DeviceSessionStore {
public:
    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Type Aliases //////////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    using ConnectionMap = std::unordered_map<winrt::hstring, DeviceConnectionInfo>;
    using DeviceIdSet = std::unordered_set<winrt::hstring>;
    using ZombieConnectionList = std::vector<winrt::Windows::Media::Audio::AudioPlaybackConnection>;

    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    [[nodiscard]] ConnectionMap& Connections() noexcept;
    [[nodiscard]] ConnectionMap const& Connections() const noexcept;
    [[nodiscard]] DeviceIdSet& DisconnectingIds() noexcept;
    [[nodiscard]] DeviceIdSet const& DisconnectingIds() const noexcept;
    [[nodiscard]] DeviceIdSet& ReconnectingIds() noexcept;
    [[nodiscard]] DeviceIdSet const& ReconnectingIds() const noexcept;
    [[nodiscard]] DeviceIdSet& ConnectingIds() noexcept;
    [[nodiscard]] DeviceIdSet const& ConnectingIds() const noexcept;
    [[nodiscard]] ZombieConnectionList& ZombieConnections() noexcept;
    [[nodiscard]] ZombieConnectionList const& ZombieConnections() const noexcept;

    void Clear();
    [[nodiscard]] std::vector<DeviceConnectionInfo> ConnectedDevices() const;
    [[nodiscard]] bool HasConnections() const;
    [[nodiscard]] bool HasBusyOperations() const;
    [[nodiscard]] bool IsDeviceBusy(winrt::hstring const& deviceId) const;

private:
    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Member Variables //////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    ConnectionMap m_connections;
    DeviceIdSet m_disconnectingIds;
    DeviceIdSet m_reconnectingIds;
    DeviceIdSet m_connectingIds;
    ZombieConnectionList m_zombieConnections;
};
