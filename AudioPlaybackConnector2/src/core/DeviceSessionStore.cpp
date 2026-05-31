#include <pch.h>

#include <core/DeviceSessionStore.hpp>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

DeviceSessionStore::ConnectionMap& DeviceSessionStore::Connections() noexcept {
    return m_connections;
}

DeviceSessionStore::ConnectionMap const& DeviceSessionStore::Connections() const noexcept {
    return m_connections;
}

DeviceSessionStore::DeviceIdSet& DeviceSessionStore::DisconnectingIds() noexcept {
    return m_disconnectingIds;
}

DeviceSessionStore::DeviceIdSet const& DeviceSessionStore::DisconnectingIds() const noexcept {
    return m_disconnectingIds;
}

DeviceSessionStore::DeviceIdSet& DeviceSessionStore::ReconnectingIds() noexcept {
    return m_reconnectingIds;
}

DeviceSessionStore::DeviceIdSet const& DeviceSessionStore::ReconnectingIds() const noexcept {
    return m_reconnectingIds;
}

DeviceSessionStore::DeviceIdSet& DeviceSessionStore::ConnectingIds() noexcept {
    return m_connectingIds;
}

DeviceSessionStore::DeviceIdSet const& DeviceSessionStore::ConnectingIds() const noexcept {
    return m_connectingIds;
}

DeviceSessionStore::ZombieConnectionList& DeviceSessionStore::ZombieConnections() noexcept {
    return m_zombieConnections;
}

DeviceSessionStore::ZombieConnectionList const& DeviceSessionStore::ZombieConnections() const noexcept {
    return m_zombieConnections;
}

void DeviceSessionStore::Clear() {
    m_connections.clear();
    m_disconnectingIds.clear();
    m_reconnectingIds.clear();
    m_connectingIds.clear();
    m_zombieConnections.clear();
}

std::vector<DeviceConnectionInfo> DeviceSessionStore::ConnectedDevices() const {
    std::vector<DeviceConnectionInfo> result;
    result.reserve(m_connections.size());
    for (const auto& entry : m_connections) {
        if (entry.second.IsOpen) result.push_back(entry.second);
    }
    return result;
}

bool DeviceSessionStore::HasConnections() const {
    return std::ranges::any_of(m_connections, [](auto const& entry) { return entry.second.IsOpen; });
}

bool DeviceSessionStore::HasBusyOperations() const {
    auto isPendingOpen = [&](winrt::hstring const& id) {
        auto iter = m_connections.find(id);
        return iter == m_connections.end() || !iter->second.IsOpen;
    };

    return std::ranges::any_of(m_connectingIds, isPendingOpen) || !m_reconnectingIds.empty();
}

bool DeviceSessionStore::IsDeviceBusy(winrt::hstring const& deviceId) const {
    auto iter = m_connections.find(deviceId);
    const bool hasConnection = iter != m_connections.end();
    const bool isOpen = hasConnection && iter->second.IsOpen;
    return m_reconnectingIds.count(deviceId) > 0 || (m_connectingIds.count(deviceId) > 0 && !isOpen) ||
           (m_disconnectingIds.count(deviceId) > 0 && hasConnection);
}
