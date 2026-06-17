#include <pch.h>

#include <app/ApplicationHost.hpp>

#include <MainWindow/MainWindow.xaml.h>
#include <app/StartupUpdateCoordinator.hpp>
#include <core/DeviceManager.hpp>
#include <core/Settings.hpp>
#include <core/StringResources.hpp>
#include <core/ThemeHelper.hpp>
#include <ui/TrayContextMenu.hpp>
#include <ui/TrayIcon.hpp>
#include <util/CrashHandler.hpp>
#include <util/Util.hpp>

#include <utility>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Helpers ///////////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

namespace {} // namespace

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

ApplicationHost::ApplicationHost() {
    util::crash::InstallCrashHandlers();
}

ApplicationHost::~ApplicationHost() {
    Shutdown();
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void ApplicationHost::Start() {
    if (!m_singleInstanceGuard.TryAcquire(L"AudioPlaybackConnector2_SingleInstance_v2")) {
        ExitProcess(0);
        return;
    }
    DebugTrace(L"[App] OnLaunched started");
    SetupMainWindow();
}

void ApplicationHost::PerformTeardown(bool saveLastConnected) noexcept {
    if (m_exiting.exchange(true)) return;

    m_powerTransitionCoordinator.Cancel();
    m_settingsWindowPresenter.Close();
    TeardownDeviceEvents();
    if (m_hwnd) {
        try {
            KillTimer(m_hwnd, c_timerAnimation);
            RemoveWindowSubclass(m_hwnd, SubclassProc, 1);
        } catch (...) {
        }
    }
    if (m_trayController) {
        m_trayController->Teardown();
    }
    if (m_notificationService) {
        m_notificationService->Teardown();
    }
    if (saveLastConnected) {
        SaveLastConnectedDevices(/*saveImmediately=*/true);
    }
    if (m_deviceManager) {
        m_deviceManager->ShutdownForProcessExit();
        m_deviceManager.reset();
    }
    m_notificationService.reset();
    m_trayController.reset();
    if (m_gdiplusToken) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    util::FlushInMemoryLogTailToFile(L"app-teardown");
}

void ApplicationHost::Shutdown() noexcept {
    PerformTeardown(/*saveLastConnected=*/false);
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Application Launch ////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void ApplicationHost::SetupMainWindow() {
    m_mainWindow = winrt::make<winrt::AudioPlaybackConnector2::implementation::MainWindow>();
    m_dispatcherQueue = m_mainWindow.DispatcherQueue();

    // Move the window off-screen before Activate() to prevent any visible flash.
    auto appWindow = m_mainWindow.AppWindow();
    if (appWindow) {
        appWindow.Move({-32000, -32000});
        appWindow.Resize({1, 1});
    }

    auto content = m_mainWindow.Content();
    if (!content) {
        DebugTrace(L"[App] ERROR: MainWindow.Content() is null!");
        return;
    }

    auto root = content.as<Controls::Grid>();
    if (!root) {
        DebugTrace(L"[App] ERROR: MainWindow.Content() is not a Grid!");
        return;
    }

    // Hide the Grid until the window is positioned off-screen to avoid
    // a visible black/white flash during startup.
    root.Opacity(0);

    // Use Grid.Loaded instead of Window.Activated.
    // Loaded fires after the element is added to the visual tree and XamlRoot
    // has been assigned, which is required for MenuFlyout anchoring.
    auto weak = weak_from_this();
    root.Loaded([weak, root](auto&, auto&) {
        if (auto self = weak.lock()) {
            self->OnMainWindowLoaded(root);
        }
    });

    m_mainWindow.Activate();
    DebugTrace(L"[App] MainWindow.Activate() called");
}

void ApplicationHost::OnMainWindowLoaded(Controls::Grid const& root) {
    if (m_hwnd) {
        DebugTrace(L"[App] Grid.Loaded fired again, ignoring (already initialized)");
        return;
    }
    DebugTrace(L"[App] Grid.Loaded - beginning initialization");

    m_hwnd = util::GetWindowHandle(m_mainWindow);
    if (!m_hwnd) {
        DebugTrace(L"[App] ERROR: GetWindowHandle returned null!");
        return;
    }
    DebugTrace(L"[App] MainWindow HWND = 0x{0:X}", reinterpret_cast<uintptr_t>(m_hwnd));

    auto exStyle = GetWindowLongPtr(m_hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(m_hwnd, GWL_EXSTYLE, exStyle | WS_EX_TOOLWINDOW);
    ShowWindow(m_hwnd, SW_SHOWNA);
    root.Opacity(1);
    DebugTrace(L"[App] MainWindow moved off-screen (toolwindow, showna)");

    SetWindowSubclass(m_hwnd, SubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    DebugTrace(L"[App] Window subclass installed");

    m_settings = std::make_shared<::Settings>();
    m_settings->Load(GetModuleHandleW(nullptr));
    DebugTrace(L"[App] Settings loaded");

    {
        auto locked = m_settings->LockSharedData();
        StringResources::Instance().Initialize(GetModuleHandleW(nullptr), locked->Language);
    }
    DebugTrace(L"[App] StringResources initialized");

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, nullptr) != Gdiplus::Ok) {
        DebugTrace(L"[App] ERROR: GdiplusStartup failed");
        ExitApplication();
        return;
    }
    DebugTrace(L"[App] GDI+ initialized");
    auto gdiplusGuard = wil::scope_exit([this]() {
        if (m_gdiplusToken) {
            Gdiplus::GdiplusShutdown(m_gdiplusToken);
            m_gdiplusToken = 0;
        }
    });

    InitializeDeviceManager();
    InitializeTray();
    InitializeNotifications();
    SetupDeviceEvents();
    m_deviceManager->StartDeviceWatcher();
    DebugTrace(L"[App] Device watcher started");
    bool willAutoReconnect = false;
    {
        auto locked = m_settings->LockSharedData();
        bool globalAutoReconnect = locked->GlobalAutoReconnect;
        for (const auto& id : locked->LastConnectedIds) {
            auto it = std::ranges::find_if(locked->Devices, [&](const auto& d) { return d.Id == id; });
            if (it != locked->Devices.end() && (globalAutoReconnect || it->AutoReconnect)) {
                willAutoReconnect = true;
                break;
            }
        }
    }

    if (m_notificationService && !willAutoReconnect) {
        m_notificationService->ShowAppStarted();
    }
    TryAutoReconnect();
    RefreshTrayVisualState(false);

    gdiplusGuard.release();

    s_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    m_trayController->UpdateTooltipFromConnections();
    CheckForUpdatesOnStartupAsync();
    util::crash::CheckAndPromptCrashReports();
    DebugTrace(L"[App] Initialization complete");
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Initializers //////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void ApplicationHost::InitializeTray() {
    DebugTrace(L"[App] InitializeTray()");
    m_trayController = std::make_shared<TrayController>();
    m_trayController->Initialize(m_hwnd, m_mainWindow);
    m_trayController->SetDeviceManager(m_deviceManager);
    auto weak = weak_from_this();
    m_trayController->SetCallbacks(
        [weak]() {
            if (auto self = weak.lock()) self->ShowSettingsWindow();
        },
        [weak]() {
            if (auto self = weak.lock()) self->ExitApplication();
        },
        [weak](winrt::hstring id) {
            if (auto self = weak.lock(); self && self->m_deviceManager) self->m_deviceManager->ConnectDetached(id);
        },
        [weak](winrt::hstring id) {
            if (auto self = weak.lock(); self && self->m_deviceManager) self->m_deviceManager->Disconnect(id);
        },
        [weak](winrt::hstring id) {
            if (auto self = weak.lock(); self && self->m_deviceManager) self->m_deviceManager->ReconnectDetached(id);
        },
        [weak]() {
            if (auto self = weak.lock()) self->ToggleLastConnectedDeviceFromTray();
        },
        [weak]() {
            if (auto self = weak.lock(); self && self->m_deviceManager) self->m_deviceManager->DisconnectAll();
        },
        [weak]() {
            if (auto self = weak.lock(); self && self->m_deviceManager) self->m_deviceManager->ReconnectAll();
        });
    m_trayController->PreloadDevicePicker();
    DebugTrace(L"[App] TrayController initialized");
}

void ApplicationHost::InitializeNotifications() {
    DebugTrace(L"[App] InitializeNotifications()");
    m_notificationService = std::make_shared<NotificationService>();
    auto weak = weak_from_this();
    m_notificationService->SetShouldShowNotificationCallback([weak]() -> bool {
        if (auto self = weak.lock()) {
            if (!self->m_settings) return true;
            auto locked = self->m_settings->LockSharedData();
            return locked->ShowNotifications;
        }
        return true;
    });
    m_notificationService->SetReconnectCallback([weak](winrt::hstring deviceId) {
        if (auto self = weak.lock()) {
            self->RunOnUIThread([weak, deviceId = std::move(deviceId)]() mutable {
                if (auto self = weak.lock()) {
                    if (self->m_exiting.load() || !self->m_deviceManager) return;
                    self->m_deviceManager->ReconnectDetached(deviceId);
                }
            });
        }
    });
    m_notificationsAvailable = m_notificationService->Initialize(
        winrt::hstring(_("AppName")), winrt::Windows::Foundation::Uri(L"ms-appx:///Images/Square44x44Logo.png"));
    DebugTrace(L"[App] Notifications available: {0}", m_notificationsAvailable);
}

void ApplicationHost::InitializeDeviceManager() {
    DebugTrace(L"[App] InitializeDeviceManager()");
    m_deviceManager = std::make_shared<DeviceManager>();
    m_settingsController = std::make_shared<SettingsController>(m_settings, m_deviceManager);
    auto weak = weak_from_this();
    m_deviceManager->SetAutoReconnectPredicate([weak](auto id) {
        auto self = weak.lock();
        if (!self || self->m_exiting.load() || !self->m_settings) return false;
        auto locked = self->m_settings->LockSharedData();
        if (locked->GlobalAutoReconnect) return true;
        return std::ranges::any_of(locked->Devices, [&](const auto& d) { return d.Id == id && d.AutoReconnect; });
    });
    DebugTrace(L"[App] DeviceManager initialized");
}

winrt::hstring ApplicationHost::ResolveKnownDeviceName(winrt::hstring const& id) const {
    if (!m_settings) return id;
    auto locked = m_settings->LockSharedData();
    auto it = std::ranges::find_if(locked->Devices, [&](const auto& device) { return device.Id == id; });
    if (it != locked->Devices.end()) return winrt::hstring(it->Name);
    return id;
}

void ApplicationHost::ToggleLastConnectedDeviceFromTray() {
    if (m_exiting.load() || !m_settings || !m_deviceManager) return;

    if (m_deviceManager->HasBusyOperations()) {
        DebugTrace(L"[App] Tray double-click ignored: device operation in progress");
        return;
    }

    std::wstring targetId;
    {
        auto locked = m_settings->LockSharedData();
        if (locked->LastConnectedIds.empty()) {
            DebugTrace(L"[App] Tray double-click ignored: no last connected device");
            return;
        }
        targetId = locked->LastConnectedIds.front();
    }

    if (targetId.empty()) return;

    auto id = winrt::hstring(targetId);

    if (m_deviceManager->IsDeviceBusy(id)) {
        DebugTrace(L"[App] Tray double-click ignored: device busy: {0}", targetId);
        return;
    }

    if (m_deviceManager->IsDeviceConnected(id)) {
        DebugTrace(L"[App] Tray double-click: disconnecting {0}", targetId);
        m_deviceManager->Disconnect(id);
    } else {
        DebugTrace(L"[App] Tray double-click: connecting {0}", targetId);
        m_deviceManager->ConnectDetached(id);
    }
    RefreshTrayVisualState(false);
}

void ApplicationHost::TryAutoReconnect() {
    if (m_exiting.load() || !m_settings || !m_deviceManager) return;

    DebugTrace(L"[App] TryAutoReconnect()");
    bool globalAutoReconnect = false;
    std::vector<std::wstring> lastConnectedIds;
    std::vector<DeviceSettings> devices;
    {
        auto locked = m_settings->LockSharedData();
        globalAutoReconnect = locked->GlobalAutoReconnect;
        lastConnectedIds = locked->LastConnectedIds;
        devices = locked->Devices;
    }

    for (const auto& id : lastConnectedIds) {
        auto device = std::ranges::find_if(devices, [&](const auto& knownDevice) { return knownDevice.Id == id; });
        if (device != devices.end() && (globalAutoReconnect || device->AutoReconnect)) {
            DebugTrace(L"[App] Auto-reconnecting to: {0}", id);
            m_deviceManager->ConnectDetached(winrt::hstring(id));
        }
    }
}

void ApplicationHost::SaveLastConnectedDevices(bool saveImmediately) {
    try {
        if (!m_settings || !m_deviceManager) return;

        auto connected = m_deviceManager->GetConnectedDevices();
        {
            auto locked = m_settings->LockExclusiveData();
            locked->LastConnectedIds.clear();
            for (const auto& c : connected) {
                if (!c.Id.empty()) {
                    locked->LastConnectedIds.push_back(c.Id);
                }
            }
        }
        if (saveImmediately) {
            m_settings->Save(GetModuleHandleW(nullptr));
        } else {
            ScheduleDeferredSettingsSave();
        }
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] SaveLastConnectedDevices ERROR", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] SaveLastConnectedDevices ERROR", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[App] SaveLastConnectedDevices ERROR");
    }
}

void ApplicationHost::HandlePowerSuspend() {
    auto weak = weak_from_this();
    m_powerTransitionCoordinator.HandleSuspend(
        [weak]() {
            if (auto self = weak.lock()) {
                self->SaveLastConnectedDevices(/*saveImmediately=*/true);
            }
        },
        m_deviceManager);
}

void ApplicationHost::HandlePowerResume() {
    auto weak = weak_from_this();
    m_powerTransitionCoordinator.HandleResume(m_deviceManager, [weak]() {
        if (auto self = weak.lock()) {
            self->RunOnUIThread([weak]() {
                if (auto self = weak.lock()) {
                    self->TryAutoReconnect();
                }
            });
        }
    });
}

winrt::fire_and_forget ApplicationHost::CheckForUpdatesOnStartupAsync() {
    auto lifetime = shared_from_this();
    auto settings = m_settings;
    auto notificationService = m_notificationService;
    if (m_exiting.load() || !settings || !notificationService) co_return;
    co_await StartupUpdateCoordinator::CheckForUpdatesAsync(*settings, notificationService, m_exiting);
}

void ApplicationHost::RunOnUIThread(std::function<void()> work) {
    if (m_exiting.load()) return;
    if (!m_dispatcherQueue) return;
    if (m_dispatcherQueue.HasThreadAccess()) {
        if (m_exiting.load()) return;
        work();
    } else {
        auto weak = weak_from_this();
        (void)m_dispatcherQueue.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueuePriority::Normal,
                                           [weak, work = std::move(work)]() {
                                               auto self = weak.lock();
                                               if (!self || self->m_exiting.load()) return;
                                               work();
                                           });
    }
}

void ApplicationHost::RefreshTrayVisualState(bool forceErrorWhenIdle) {
    if (m_exiting.load() || !m_trayController || !m_deviceManager) return;
    if (!m_hwnd || !IsWindow(m_hwnd)) return;

    const bool hasBusyOperations = m_deviceManager->HasBusyOperations();
    const bool hasConnections = m_deviceManager->HasConnections();

    if (forceErrorWhenIdle) {
        KillTimer(m_hwnd, c_timerAnimation);
        m_trayController->SetState(hasConnections ? TrayIconState::Connected : TrayIconState::Error);
    } else if (hasBusyOperations) {
        m_trayController->SetState(TrayIconState::Connecting);
        SetTimer(m_hwnd, c_timerAnimation, 200, nullptr);
    } else {
        KillTimer(m_hwnd, c_timerAnimation);
        m_trayController->SetState(hasConnections ? TrayIconState::Connected : TrayIconState::Idle);
    }

    if (!forceErrorWhenIdle) {
        m_trayController->UpdateTooltipFromConnections();
    }
    m_trayController->RefreshDevicePickerState();
}

void ApplicationHost::SetupDeviceEvents() {
    DebugTrace(L"[App] SetupDeviceEvents()");
    auto weak = weak_from_this();
    DeviceEventRouter::Callbacks callbacks;
    callbacks.DeviceConnected = [weak](auto const& id) {
        if (auto self = weak.lock()) self->OnDeviceConnected(id);
    };
    callbacks.DeviceDisconnected = [weak](auto const& id) {
        if (auto self = weak.lock()) self->OnDeviceDisconnected(id);
    };
    callbacks.ConnectionError = [weak](auto const& id, auto const& msg) {
        if (auto self = weak.lock()) self->OnConnectionError(id, msg);
    };
    callbacks.AutoReconnectTriggered = [weak](auto const& id) {
        if (auto self = weak.lock()) self->OnAutoReconnectTriggered(id);
    };
    callbacks.AutoReconnectFailed = [weak](auto const& id) {
        if (auto self = weak.lock()) self->OnAutoReconnectFailed(id);
    };
    callbacks.DeviceStatusChanged = [weak](auto const&, auto const&, DeviceStatusKind statusKind) {
        auto self = weak.lock();
        if (!self) return;
        if (self->m_exiting.load() || !self->m_trayController || !self->m_deviceManager) return;
        if (!self->m_hwnd || !IsWindow(self->m_hwnd)) return;
        self->RefreshTrayVisualState(statusKind == DeviceStatusKind::Error);
    };
    callbacks.DeviceActivityChanged = [weak]() {
        auto self = weak.lock();
        if (!self || self->m_exiting.load() || !self->m_trayController || !self->m_deviceManager) return;
        if (!self->m_hwnd || !IsWindow(self->m_hwnd)) return;
        self->RefreshTrayVisualState(false);
    };

    m_deviceEventRouter.Attach(
        m_deviceManager,
        [weak](std::function<void()> work) {
            if (auto self = weak.lock()) {
                self->RunOnUIThread(std::move(work));
            }
        },
        std::move(callbacks));
}

void ApplicationHost::TeardownDeviceEvents() {
    m_deviceEventRouter.Detach();
}

void ApplicationHost::ShowSettingsWindow() {
    if (m_exiting.load()) return;
    DebugTrace(L"[App] ShowSettingsWindow()");
    auto weak = weak_from_this();
    m_settingsWindowPresenter.Show(m_settingsController, m_trayController, [weak]() {
        auto self = weak.lock();
        if (!self || !self->m_settings) return;
        self->m_settings->Save(GetModuleHandleW(nullptr));
    });
}

void ApplicationHost::ExitApplication() {
    DebugTrace(L"[App] ExitApplication() started");
    PerformTeardown(/*saveLastConnected=*/true);

    m_settingsWindowPresenter.Close();

    if (m_mainWindow) {
        m_mainWindow.Close();
    }
    DebugTrace(L"[App] ExitApplication() complete");
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Device Event Handlers /////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void ApplicationHost::ScheduleDeferredSettingsSave() {
    if (m_exiting.load() || !m_settings) return;

    bool expected = false;
    if (!m_settingsSavePending.compare_exchange_strong(expected, true)) return;

    auto weak = weak_from_this();
    auto module = GetModuleHandleW(nullptr);
    [](std::weak_ptr<ApplicationHost> weak, HINSTANCE module) -> winrt::fire_and_forget {
        co_await winrt::resume_after(std::chrono::milliseconds(300));
        auto self = weak.lock();
        if (!self) co_return;
        self->m_settingsSavePending = false;
        if (self->m_exiting.load() || !self->m_settings) co_return;
        try {
            self->m_settings->Save(module);
        } catch (winrt::hresult_error const& ex) {
            util::DebugTraceException(L"[App] Deferred settings save ERROR", ex);
        } catch (std::exception const& ex) {
            util::DebugTraceException(L"[App] Deferred settings save ERROR", ex);
        } catch (...) {
            util::DebugTraceUnknownException(L"[App] Deferred settings save ERROR");
        }
    }(std::move(weak), module);
}

void ApplicationHost::OnDeviceConnected(winrt::hstring const& id) {
    if (m_exiting.load() || !m_settings || !m_deviceManager || !m_notificationService || !m_trayController) return;
    DebugTrace(L"[App] OnDeviceConnected: {0}", std::wstring(id));

    if (!m_deviceManager->IsDeviceConnected(id)) {
        return;
    }

    winrt::hstring deviceName = ResolveKnownDeviceName(id);
    if (auto displayName = m_deviceManager->GetConnectionDisplayName(id)) {
        deviceName = winrt::hstring(*displayName);
    }

    bool addedNew = false;
    bool updatedLastConnected = false;
    try {
        auto locked = m_settings->LockExclusiveData();
        bool alreadyKnown = std::ranges::any_of(locked->Devices, [&](const auto& d) { return d.Id == id; });
        if (!alreadyKnown) {
            DeviceSettings newDevice;
            newDevice.Id = std::wstring(id);
            newDevice.Name = std::wstring(deviceName);
            newDevice.AutoReconnect = locked->GlobalAutoReconnect;
            locked->Devices.push_back(std::move(newDevice));
            addedNew = true;
        } else {
            auto existingDevice = std::ranges::find_if(locked->Devices, [&](const auto& d) { return d.Id == id; });
            if (existingDevice != locked->Devices.end() && existingDevice->Name != deviceName) {
                existingDevice->Name = std::wstring(deviceName);
            }
        }

        auto existing = std::ranges::find(locked->LastConnectedIds, std::wstring(id));
        if (existing != locked->LastConnectedIds.end()) {
            locked->LastConnectedIds.erase(existing);
        }
        locked->LastConnectedIds.insert(locked->LastConnectedIds.begin(), std::wstring(id));
        updatedLastConnected = true;
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] OnDeviceConnected settings update ERROR", ex);
        return;
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] OnDeviceConnected settings update ERROR", ex);
        return;
    } catch (...) {
        util::DebugTraceUnknownException(L"[App] OnDeviceConnected settings update ERROR");
        return;
    }

    if (addedNew || updatedLastConnected) {
        ScheduleDeferredSettingsSave();
        if (addedNew) {
            DebugTrace(L"[App] New device added to settings: {0}", std::wstring(deviceName));
        }
    }

    try {
        auto locked = m_settings->LockSharedData();
        bool autoReconnect = locked->GlobalAutoReconnect;
        auto it = std::ranges::find_if(locked->Devices, [&](const auto& d) { return d.Id == id; });
        if (it != locked->Devices.end()) autoReconnect = autoReconnect || it->AutoReconnect;
        m_deviceManager->SetAutoReconnect(id, autoReconnect);
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] OnDeviceConnected auto-reconnect sync ERROR", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] OnDeviceConnected auto-reconnect sync ERROR", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[App] OnDeviceConnected auto-reconnect sync ERROR");
    }

    try {
        m_notificationService->ShowDeviceConnected(id, deviceName);
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] OnDeviceConnected notification ERROR", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] OnDeviceConnected notification ERROR", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[App] OnDeviceConnected notification ERROR");
    }

    RefreshTrayVisualState(false);
}

void ApplicationHost::OnDeviceDisconnected(winrt::hstring const& id) {
    if (m_exiting.load() || !m_settings || !m_notificationService || !m_trayController) return;
    DebugTrace(L"[App] OnDeviceDisconnected: {0}", std::wstring(id));

    winrt::hstring deviceName = ResolveKnownDeviceName(id);
    try {
        m_notificationService->ShowDeviceDisconnected(id, deviceName);
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] OnDeviceDisconnected notification ERROR", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] OnDeviceDisconnected notification ERROR", ex);
    }

    RefreshTrayVisualState(false);
}

void ApplicationHost::OnConnectionError(winrt::hstring const& id, winrt::hstring msg) {
    if (m_exiting.load()) return;
    DebugTrace(L"[App] OnConnectionError: {0} - {1}", std::wstring(id), std::wstring(msg));
    if (m_trayController) {
        std::wstring tip = std::wstring(_("AppName")) + L"\n" + std::wstring(msg);
        m_trayController->UpdateTooltip(tip);
    }
    RefreshTrayVisualState(true);
}

void ApplicationHost::OnAutoReconnectTriggered(winrt::hstring const& id) {
    if (m_exiting.load() || !m_settings || !m_notificationService) return;
    DebugTrace(L"[App] OnAutoReconnectTriggered: {0}", std::wstring(id));

    winrt::hstring deviceName = ResolveKnownDeviceName(id);
    try {
        m_notificationService->ShowAutoReconnect(id, deviceName);
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] OnAutoReconnectTriggered notification ERROR", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] OnAutoReconnectTriggered notification ERROR", ex);
    }
}

void ApplicationHost::OnAutoReconnectFailed(winrt::hstring const& id) {
    if (m_exiting.load() || !m_settings || !m_notificationService) return;
    DebugTrace(L"[App] OnAutoReconnectFailed: {0}", std::wstring(id));

    winrt::hstring deviceName = ResolveKnownDeviceName(id);
    try {
        m_notificationService->ShowAutoReconnectFailed(id, deviceName);
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[App] OnAutoReconnectFailed notification ERROR", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[App] OnAutoReconnectFailed notification ERROR", ex);
    }
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Window Subclass ///////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

LRESULT CALLBACK
ApplicationHost::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR dwRefData) {
    auto* host = reinterpret_cast<ApplicationHost*>(dwRefData);
    if (!host) return DefSubclassProc(hwnd, msg, wParam, lParam);
    if (host->m_exiting.load()) return DefSubclassProc(hwnd, msg, wParam, lParam);

    if (host->m_trayController && msg == host->m_trayController->TrayCallbackMessage()) {
        host->m_trayController->HandleTrayMessage(wParam, lParam);
        return 0;
    }

    if (msg == WM_SETTINGCHANGE) {
        ThemeHelper::OnSettingChange(hwnd, lParam);
        return 0;
    }

    if (msg == WM_POWERBROADCAST) {
        switch (wParam) {
            case PBT_APMSUSPEND: host->HandlePowerSuspend(); return TRUE;
            case PBT_APMRESUMEAUTOMATIC:
            case PBT_APMRESUMESUSPEND: host->HandlePowerResume(); return TRUE;
            default: break;
        }
    }

    if (msg == WM_TIMER && wParam == c_timerAnimation && host->m_trayController) {
        host->m_trayController->ToggleConnectingFrame();
        return 0;
    }

    if (s_wmTaskbarCreated && msg == s_wmTaskbarCreated) {
        if (host->m_trayController) {
            host->m_trayController->Reregister();
            host->m_trayController->OnThemeChanged();
        }
        return 0;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
