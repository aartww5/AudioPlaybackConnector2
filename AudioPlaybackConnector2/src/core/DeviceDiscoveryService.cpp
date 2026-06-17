#include <pch.h>

#include <core/DeviceDiscoveryService.hpp>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

DeviceDiscoveryService::~DeviceDiscoveryService() {
    Stop();
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DeviceDiscoveryService::Start() {
    std::lock_guard lifecycleLock(m_watcherLifecycleMutex);
    if (m_watcher) return;

    std::uint64_t watcherGeneration = 0;
    {
        auto guard = m_lock.lock_exclusive();
        watcherGeneration = ++m_watcherGeneration;
        m_watcherStopping = false;
    }

    winrt::Windows::Devices::Enumeration::DeviceWatcher watcher{nullptr};
    winrt::event_token addedToken{};
    winrt::event_token removedToken{};
    try {
        auto weak = weak_from_this();
        auto selector = winrt::Windows::Media::Audio::AudioPlaybackConnection::GetDeviceSelector();
        watcher = winrt::Windows::Devices::Enumeration::DeviceInformation::CreateWatcher(selector);
        addedToken = watcher.Added([weak, watcherGeneration](auto const& sender, auto const& args) {
            if (auto self = weak.lock()) {
                self->OnDeviceAdded(watcherGeneration, sender, args);
            }
        });
        removedToken = watcher.Removed([weak, watcherGeneration](auto const& sender, auto const& args) {
            if (auto self = weak.lock()) {
                self->OnDeviceRemoved(watcherGeneration, sender, args);
            }
        });
        watcher.Start();
        m_watcher = watcher;
        m_watcherAddedToken = addedToken;
        m_watcherRemovedToken = removedToken;
        DebugTrace(L"[DeviceDiscoveryService] DeviceWatcher started");
    } catch (winrt::hresult_error const& ex) {
        if (watcher) {
            try {
                if (addedToken.value != 0) watcher.Added(addedToken);
                if (removedToken.value != 0) watcher.Removed(removedToken);
            } catch (...) {
            }
        }
        util::DebugTraceException(L"[DeviceDiscoveryService] Start ERROR: failed to create or start watcher", ex);
    } catch (std::exception const& ex) {
        if (watcher) {
            try {
                if (addedToken.value != 0) watcher.Added(addedToken);
                if (removedToken.value != 0) watcher.Removed(removedToken);
            } catch (...) {
            }
        }
        util::DebugTraceException(L"[DeviceDiscoveryService] Start ERROR: failed to create or start watcher", ex);
    } catch (...) {
        if (watcher) {
            try {
                if (addedToken.value != 0) watcher.Added(addedToken);
                if (removedToken.value != 0) watcher.Removed(removedToken);
            } catch (...) {
            }
        }
        util::DebugTraceUnknownException(L"[DeviceDiscoveryService] Start ERROR: failed to create or start watcher");
    }
}

void DeviceDiscoveryService::Stop() {
    std::lock_guard lifecycleLock(m_watcherLifecycleMutex);
    if (!m_watcher) return;
    auto watcher = std::exchange(m_watcher, nullptr);
    auto addedToken = std::exchange(m_watcherAddedToken, {});
    auto removedToken = std::exchange(m_watcherRemovedToken, {});
    std::uint64_t stoppedGeneration = 0;
    {
        auto guard = m_lock.lock_exclusive();
        stoppedGeneration = ++m_watcherGeneration;
        m_watcherStopping = true;
    }

    try {
        watcher.Stop();
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[DeviceDiscoveryService] Stop ERROR: failed to stop watcher", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[DeviceDiscoveryService] Stop ERROR: failed to stop watcher", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[DeviceDiscoveryService] Stop ERROR: failed to stop watcher");
    }
    try {
        if (addedToken.value != 0) watcher.Added(addedToken);
        if (removedToken.value != 0) watcher.Removed(removedToken);
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[DeviceDiscoveryService] Stop ERROR: failed to revoke watcher token", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[DeviceDiscoveryService] Stop ERROR: failed to revoke watcher token", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[DeviceDiscoveryService] Stop ERROR: failed to revoke watcher token");
    }

    {
        auto guard = m_lock.lock_exclusive();
        if (m_watcherGeneration == stoppedGeneration) {
            m_watcherStopping = false;
        }
    }
}

void DeviceDiscoveryService::ClearCache() {
    auto guard = m_lock.lock_exclusive();
    m_deviceCache.clear();
}

bool DeviceDiscoveryService::ContainsDeviceId(std::wstring const& deviceId) const {
    auto guard = m_lock.lock_shared();
    return m_deviceCache.count(deviceId) > 0;
}

std::size_t DeviceDiscoveryService::CacheSize() const {
    auto guard = m_lock.lock_shared();
    return m_deviceCache.size();
}

winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Devices::Enumeration::DeviceInformationCollection>
DeviceDiscoveryService::RefreshAsync() {
    auto selector = winrt::Windows::Media::Audio::AudioPlaybackConnection::GetDeviceSelector();
    auto devices = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(selector);

    std::unordered_set<std::wstring> refreshed;
    refreshed.reserve(static_cast<size_t>(devices.Size()));
    for (auto const& device : devices) {
        refreshed.insert(std::wstring(device.Id()));
    }

    {
        auto guard = m_lock.lock_exclusive();
        m_deviceCache = std::move(refreshed);
    }

    co_return devices;
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Private Implementation ////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DeviceDiscoveryService::OnDeviceAdded(std::uint64_t watcherGeneration,
                                           winrt::Windows::Devices::Enumeration::DeviceWatcher const&,
                                           winrt::Windows::Devices::Enumeration::DeviceInformation const& args) {
    {
        auto guard = m_lock.lock_exclusive();
        if (m_watcherStopping || watcherGeneration != m_watcherGeneration) return;
        m_deviceCache.insert(std::wstring(args.Id()));
    }
    DeviceAdded(args);
}

void DeviceDiscoveryService::OnDeviceRemoved(
    std::uint64_t watcherGeneration,
    winrt::Windows::Devices::Enumeration::DeviceWatcher const&,
    winrt::Windows::Devices::Enumeration::DeviceInformationUpdate const& args) {
    {
        auto guard = m_lock.lock_exclusive();
        if (m_watcherStopping || watcherGeneration != m_watcherGeneration) return;
        m_deviceCache.erase(std::wstring(args.Id()));
    }
    DeviceRemoved(args);
}
