#include <pch.h>
#include <ui/SettingsWindow/SettingsWindow.xaml.h>
#if __has_include("SettingsWindow.g.cpp")
#include <SettingsWindow.g.cpp>
#endif

#include <core/Settings.hpp>
#include <core/StringResources.hpp>
#include <core/ThemeHelper.hpp>
#include <services/StartupTaskController.hpp>
#include <services/UpdateService.hpp>
#include <ui/ButtonHelpers.hpp>
#include <util/Util.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Windowing;

namespace {
constexpr auto c_placementSaveDelay = std::chrono::seconds(3);

std::wstring BuildVersionText() {
    std::wstring label(_("About_Version"));
    try {
        auto version = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
        auto versionText =
            version.Revision == 0
                ? std::format(L"{}.{}.{}", version.Major, version.Minor, version.Build)
                : std::format(L"{}.{}.{}.{}", version.Major, version.Minor, version.Build, version.Revision);
        return std::format(L"{} {}", label, versionText);
    } catch (winrt::hresult_error const& ex) {
        DebugTrace(
            L"[SettingsWindow] BuildVersionText failed: 0x{0:08X} {1}", static_cast<uint32_t>(ex.code()), ex.message());
        return label;
    } catch (std::exception const& ex) {
        DebugTrace(L"[SettingsWindow] BuildVersionText failed: {0}", util::Utf8ToUtf16(ex.what()));
        return label;
    } catch (...) {
        DebugTrace(L"[SettingsWindow] BuildVersionText failed: unknown exception");
        return label;
    }
}

} // namespace

namespace winrt::AudioPlaybackConnector2::implementation {

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

SettingsWindow::SettingsWindow() {
    this->SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
}

LRESULT CALLBACK SettingsWindow::SettingsWindowSubclassProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    auto self = reinterpret_cast<SettingsWindow*>(dwRefData);
    if (msg == WM_GETMINMAXINFO) {
        auto minSize = util::GetSettingsWindowMinTrackSize(hwnd);
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = minSize.cx;
        info->ptMinTrackSize.y = minSize.cy;
        return 0;
    }

    if (self && msg == WM_EXITSIZEMOVE) {
        self->QueuePlacementSave();
    } else if (self && msg == WM_WINDOWPOSCHANGED) {
        self->QueuePlacementSave();
    }

    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, SettingsWindowSubclassProc, uIdSubclass);
        if (self) {
            self->m_capturePlacementChanges = false;
            ++self->m_placementSaveRequestId;
            self->m_subclassInstalled = false;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Event Handlers ////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void SettingsWindow::RootGrid_Loaded(IInspectable const&, RoutedEventArgs const&) {
    BackdropElement().SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());

    // Localize static text before the window becomes visible.
    this->Title(winrt::hstring(_("Settings_Title")));
    TitleText().Text(winrt::hstring(_("Settings_Title")));
    SubtitleText().Text(winrt::hstring(_("Settings_Subtitle")));
    ConnectionHeader().Text(winrt::hstring(_("Settings_Connection")));
    AutoReconnectLabel().Text(winrt::hstring(_("Settings_AutoReconnect")));
    AutoReconnectDesc().Text(winrt::hstring(_("Settings_AutoReconnect_Desc")));
    SystemHeader().Text(winrt::hstring(_("Settings_System")));
    StartWithWindowsLabel().Text(winrt::hstring(_("Settings_StartWithWindows")));
    StartWithWindowsDesc().Text(winrt::hstring(_("Settings_StartWithWindows_Desc")));
    ShowNotificationsLabel().Text(winrt::hstring(_("Settings_ShowNotifications")));
    ShowNotificationsDesc().Text(winrt::hstring(_("Settings_ShowNotifications_Desc")));
    AppHeader().Text(winrt::hstring(_("Settings_App")));
    VersionText().Text(winrt::hstring(BuildVersionText()));
    CopyrightText().Text(winrt::hstring(_("About_Copyright")));
    CheckForUpdatesLabel().Text(winrt::hstring(_("Settings_CheckForUpdates")));
    CheckForUpdatesDesc().Text(winrt::hstring(_("Settings_CheckForUpdates_Desc")));
    apc::ui::SetButtonLabel(
        CheckForUpdatesButton(), CheckForUpdatesButtonText(), winrt::hstring(_("Settings_CheckForUpdates_Button")));
    apc::ui::SetButtonLabel(
        OpenAppInstallerButton(), OpenAppInstallerButtonText(), winrt::hstring(_("Settings_OpenAppInstaller")));
    WindowPlacementLabel().Text(winrt::hstring(_("Settings_WindowPlacement")));
    WindowPlacementDesc().Text(winrt::hstring(_("Settings_WindowPlacement_Desc")));
    apc::ui::SetButtonLabel(ResetWindowPlacementButton(),
                            ResetWindowPlacementButtonText(),
                            winrt::hstring(_("Settings_WindowPlacement_Reset")));

    InitializeSettingsContent();

    // Extend content into title bar for seamless Mica look (Windows 11 style).
    this->ExtendsContentIntoTitleBar(true);
    this->SetTitleBar(TitleBarArea());

    auto hwnd = util::GetWindowHandle(*this);
    if (hwnd) {
        util::ApplyNativeMicaBackdrop(hwnd);

        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        BOOL dark = ThemeHelper::GetSystemTheme() == Theme::Dark;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        auto appWindow = this->AppWindow();
        if (appWindow) {
            appWindow.Resize({m_targetPlacement.size.cx, m_targetPlacement.size.cy});

            auto presenter = appWindow.Presenter().as<OverlappedPresenter>();
            if (presenter) {
                presenter.IsResizable(true);
                presenter.IsMinimizable(false);
                presenter.IsMaximizable(false);
            }

            if (!m_subclassInstalled &&
                SetWindowSubclass(hwnd, SettingsWindowSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
                m_subclassInstalled = true;
            }

            RevealAtTarget(hwnd);
        }
    }
}

void SettingsWindow::RevealAtTarget(HWND hwnd) {
    auto appWindow = this->AppWindow();
    if (appWindow) {
        appWindow.Move({m_targetPlacement.position.x, m_targetPlacement.position.y});
        appWindow.Show();
    }
    SetForegroundWindow(hwnd);
    m_capturePlacementChanges = true;
}

void SettingsWindow::QueuePlacementSave() {
    if (!m_capturePlacementChanges) return;
    if (!StoreCurrentPlacement()) return;

    auto requestId = ++m_placementSaveRequestId;
    SavePlacementAfterDelayAsync(requestId);
}

winrt::fire_and_forget SettingsWindow::SavePlacementAfterDelayAsync(uint64_t requestId) {
    auto lifetime = get_strong();
    co_await winrt::resume_after(c_placementSaveDelay);

    if (requestId != m_placementSaveRequestId.load()) co_return;
    if (auto controller = m_settingsController) {
        controller->Save();
    }
}

bool SettingsWindow::StoreCurrentPlacement() {
    if (!m_capturePlacementChanges) return false;

    auto controller = m_settingsController;
    if (!controller) return false;

    auto hwnd = util::GetWindowHandle(*this);
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd) || IsZoomed(hwnd)) return false;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return false;

    auto width = rect.right - rect.left;
    auto height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;
    if (rect.left <= -30000 || rect.top <= -30000) return false;

    auto dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = USER_DEFAULT_SCREEN_DPI;

    return controller->SetSettingsWindowBounds(PersistedWindowBounds{static_cast<int32_t>(rect.left),
                                                                     static_cast<int32_t>(rect.top),
                                                                     static_cast<int32_t>(width),
                                                                     static_cast<int32_t>(height),
                                                                     dpi});
}

void SettingsWindow::StartWithWindowsToggle_Toggled(IInspectable const& sender, RoutedEventArgs const&) {
    if (m_suppressStartupToggle) return;
    auto toggle = sender.as<ToggleSwitch>();
    ApplyStartWithWindowsAsync(toggle.IsOn());
}

void SettingsWindow::CheckForUpdatesButton_Click(IInspectable const&, RoutedEventArgs const&) {
    RunManualUpdateCheckAsync();
}

void SettingsWindow::OpenAppInstallerButton_Click(IInspectable const&, RoutedEventArgs const&) {
    UpdateService::LaunchAppInstallerAsync();
}

void SettingsWindow::ResetWindowPlacementButton_Click(IInspectable const&, RoutedEventArgs const&) {
    ResetWindowPlacement();
}

void SettingsWindow::ResetWindowPlacement() {
    ++m_placementSaveRequestId;
    m_capturePlacementChanges = false;

    if (auto controller = m_settingsController) {
        if (controller->ClearSettingsWindowBounds()) {
            controller->Save();
        }
    }

    m_targetPlacement = m_defaultPlacement;
    auto appWindow = this->AppWindow();
    if (appWindow) {
        appWindow.Resize({m_defaultPlacement.size.cx, m_defaultPlacement.size.cy});
        appWindow.Move({m_defaultPlacement.position.x, m_defaultPlacement.position.y});
        appWindow.Show();
    }

    if (auto hwnd = util::GetWindowHandle(*this)) {
        SetForegroundWindow(hwnd);
    }

    m_capturePlacementChanges = true;
}

void SettingsWindow::InitializeSettingsContent() {
    if (m_contentInitialized) return;
    m_contentInitialized = true;

    auto controller = m_settingsController;
    if (!controller) return;

    {
        auto settings = controller->Snapshot();
        AutoReconnectToggle().IsOn(settings.GlobalAutoReconnect);
    }
    AutoReconnectToggle().OffContent(box_value(L""));
    AutoReconnectToggle().OnContent(box_value(L""));
    auto weak = get_weak();
    AutoReconnectToggle().Toggled([weak](auto const& s, auto) {
        if (auto self = weak.get()) {
            if (auto settingsController = self->m_settingsController) {
                settingsController->SetGlobalAutoReconnect(s.template as<ToggleSwitch>().IsOn());
            }
            self->RebuildDeviceList();
        }
    });

    // Show cached value immediately; async init below corrects it from the actual task state.
    {
        auto settings = controller->Snapshot();
        StartWithWindowsToggle().IsOn(settings.StartWithWindows);
        ShowNotificationsToggle().IsOn(settings.ShowNotifications);
    }
    StartWithWindowsToggle().OffContent(box_value(L""));
    StartWithWindowsToggle().OnContent(box_value(L""));
    ShowNotificationsToggle().OffContent(box_value(L""));
    ShowNotificationsToggle().OnContent(box_value(L""));
    SyncStartupTaskStateAsync();

    StartWithWindowsToggle().Toggled([weak](auto const& sender, auto const& args) {
        if (auto self = weak.get()) {
            self->StartWithWindowsToggle_Toggled(sender, args);
        }
    });

    ShowNotificationsToggle().Toggled([weak](auto const& s, auto) {
        if (auto self = weak.get()) {
            if (auto settingsController = self->m_settingsController) {
                settingsController->SetShowNotifications(s.template as<ToggleSwitch>().IsOn());
            }
        }
    });

    RebuildDeviceList();
}

winrt::fire_and_forget SettingsWindow::RunManualUpdateCheckAsync() {
    auto lifetime = get_strong();
    auto requestId = ++m_updateCheckRequestId;
    SetUpdateCheckBusy(true);

    winrt::apartment_context ui;
    auto result = co_await UpdateService::CheckForUpdatesAsync();
    co_await ui;

    if (requestId != m_updateCheckRequestId.load()) co_return;
    SetUpdateCheckBusy(false);
    ShowUpdateCheckResult(result);
}

void SettingsWindow::SetUpdateCheckBusy(bool busy) {
    CheckForUpdatesButton().IsEnabled(!busy);
    UpdateCheckProgress().IsActive(busy);
    UpdateCheckProgress().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
    if (busy) {
        OpenAppInstallerButton().Visibility(Visibility::Collapsed);
        UpdateInfoBar().IsOpen(false);
    }
}

void SettingsWindow::SetStartupTaskBusy(bool busy) {
    StartWithWindowsToggle().IsEnabled(!busy);
    StartupTaskProgress().IsActive(busy);
    StartupTaskProgress().Visibility(busy ? Visibility::Visible : Visibility::Collapsed);
}

void SettingsWindow::ShowUpdateCheckResult(UpdateCheckResult const& result) {
    OpenAppInstallerButton().Visibility(Visibility::Collapsed);

    switch (result.Status) {
        case UpdateCheckStatus::UpdateAvailable:
            UpdateInfoBar().Severity(InfoBarSeverity::Informational);
            UpdateInfoBar().Title(winrt::hstring(_("Settings_UpdateAvailable_Title")));
            UpdateInfoBar().Message(
                winrt::hstring(util::ReplacePlaceholders(_("Settings_UpdateAvailable_Message"), result.LatestVersion)));
            OpenAppInstallerButton().Visibility(Visibility::Visible);
            break;
        case UpdateCheckStatus::UpToDate:
            UpdateInfoBar().Severity(InfoBarSeverity::Success);
            UpdateInfoBar().Title(winrt::hstring(_("Settings_UpdateCurrent_Title")));
            UpdateInfoBar().Message(winrt::hstring(_("Settings_UpdateCurrent_Message")));
            break;
        case UpdateCheckStatus::Failed:
        default:
            UpdateInfoBar().Severity(InfoBarSeverity::Error);
            UpdateInfoBar().Title(winrt::hstring(_("Settings_UpdateFailed_Title")));
            if (!result.ErrorMessage.empty()) {
                DebugTrace(L"[SettingsWindow] Manual update check failed: {0}", result.ErrorMessage);
            }
            UpdateInfoBar().Message(winrt::hstring(_("Settings_UpdateFailed_Message")));
            break;
    }

    UpdateInfoBar().IsOpen(true);
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Startup Integration ///////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

winrt::fire_and_forget SettingsWindow::SyncStartupTaskStateAsync() {
    auto lifetime = get_strong();
    winrt::apartment_context ui;
    auto requestId = m_startupRequestId.load();
    SetStartupTaskBusy(true);

    try {
        bool enabled = co_await StartupTaskController::IsEnabledAsync();

        co_await ui;
        if (requestId != m_startupRequestId.load()) co_return;
        SetStartupTaskBusy(false);

        m_suppressStartupToggle = true;
        StartWithWindowsToggle().IsOn(enabled);
        m_suppressStartupToggle = false;
        if (auto settingsController = m_settingsController) {
            settingsController->SetStartWithWindows(enabled);
        }
        co_return;
    } catch (winrt::hresult_error const& ex) {
        DebugTrace(L"[SettingsWindow] SyncStartupTaskStateAsync failed: 0x{0:08X} {1}",
                   static_cast<uint32_t>(ex.code()),
                   ex.message());
    } catch (std::exception const& ex) {
        DebugTrace(L"[SettingsWindow] SyncStartupTaskStateAsync failed: {0}", util::Utf8ToUtf16(ex.what()));
    } catch (...) {
        DebugTrace(L"[SettingsWindow] SyncStartupTaskStateAsync failed: unknown exception");
    }

    co_await ui;
    if (requestId == m_startupRequestId.load()) {
        SetStartupTaskBusy(false);
    }
}

winrt::fire_and_forget SettingsWindow::ApplyStartWithWindowsAsync(bool on) {
    auto lifetime = get_strong();
    winrt::apartment_context ui;
    auto requestId = ++m_startupRequestId;
    bool revertToggle = false;
    SetStartupTaskBusy(true);

    try {
        bool success = co_await StartupTaskController::SetEnabledAsync(on);

        co_await ui;
        if (requestId != m_startupRequestId.load()) co_return;
        SetStartupTaskBusy(false);

        if (!success) {
            if (auto settingsController = m_settingsController) {
                settingsController->SetStartWithWindows(!on);
            }
            m_suppressStartupToggle = true;
            StartWithWindowsToggle().IsOn(!on);
            m_suppressStartupToggle = false;
            co_return;
        }

        if (auto settingsController = m_settingsController) {
            settingsController->SetStartWithWindows(on);
        }
    } catch (winrt::hresult_error const& ex) {
        DebugTrace(L"[SettingsWindow] ApplyStartWithWindowsAsync failed: 0x{0:08X} {1}",
                   static_cast<uint32_t>(ex.code()),
                   ex.message());
        revertToggle = true;
    } catch (std::exception const& ex) {
        DebugTrace(L"[SettingsWindow] ApplyStartWithWindowsAsync failed: {0}", util::Utf8ToUtf16(ex.what()));
        revertToggle = true;
    } catch (...) {
        DebugTrace(L"[SettingsWindow] ApplyStartWithWindowsAsync failed: unknown exception");
        revertToggle = true;
    }

    if (revertToggle) {
        co_await ui;
        if (requestId != m_startupRequestId.load()) co_return;
        SetStartupTaskBusy(false);
        if (auto settingsController = m_settingsController) {
            settingsController->SetStartWithWindows(!on);
        }
        m_suppressStartupToggle = true;
        StartWithWindowsToggle().IsOn(!on);
        m_suppressStartupToggle = false;
    }
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Private Implementation ////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void SettingsWindow::RebuildDeviceList() {
    DevicesPanel().Children().Clear();

    auto controller = m_settingsController;
    if (!controller) return;

    auto secondaryBrush =
        apc::ui::ThemeBrushOrFallback(L"TextFillColorSecondaryBrush", winrt::Windows::UI::Colors::Gray());
    auto rowBackgroundBrush = apc::ui::ThemeBrushOrFallback(L"CardBackgroundFillColorSecondaryBrush",
                                                            winrt::Windows::UI::Colors::Transparent());
    auto rowBorderBrush =
        apc::ui::ThemeBrushOrFallback(L"CardStrokeColorDefaultBrush", winrt::Windows::UI::Colors::Transparent());

    // Snapshot settings through the controller, then build UI without holding any settings lock.
    auto snapshot = controller->Snapshot();
    auto devices = SettingsViewModel::BuildDeviceItems(snapshot);
    bool globalAutoReconnect = snapshot.GlobalAutoReconnect;

    if (devices.empty()) {
        auto noDevices = TextBlock();
        noDevices.Text(winrt::hstring(_("Settings_NoDevices")));
        noDevices.Foreground(secondaryBrush);
        noDevices.FontSize(12);
        noDevices.TextWrapping(TextWrapping::Wrap);
        DevicesPanel().Children().Append(noDevices);
        return;
    }

    for (auto& dev : devices) {
        auto row = Border();
        row.Padding({12, 10, 12, 10});
        row.HorizontalAlignment(HorizontalAlignment::Stretch);
        row.Background(rowBackgroundBrush);
        row.BorderBrush(rowBorderBrush);
        row.BorderThickness({1, 1, 1, 1});
        row.CornerRadius({6, 6, 6, 6});

        auto item = Grid();
        item.HorizontalAlignment(HorizontalAlignment::Stretch);
        item.ColumnSpacing(12);
        item.ColumnDefinitions().Append(ColumnDefinition());
        item.ColumnDefinitions().Append(ColumnDefinition());
        item.ColumnDefinitions().GetAt(1).Width(GridLengthHelper::Auto());

        auto namePanel = StackPanel();
        namePanel.MinWidth(0);
        namePanel.VerticalAlignment(VerticalAlignment::Center);
        auto name = TextBlock();
        name.Text(dev.DisplayName);
        name.TextWrapping(TextWrapping::Wrap);
        name.TextTrimming(TextTrimming::CharacterEllipsis);
        name.MaxLines(2);
        apc::ui::SetTooltipText(name, winrt::hstring(dev.DisplayName));
        namePanel.Children().Append(name);

        auto subtitle = TextBlock();
        if (globalAutoReconnect) {
            subtitle.Text(winrt::hstring(_("Device_AutoReconnect_Global")));
        } else {
            subtitle.Text(winrt::hstring(_("Settings_PairedDevice")));
        }
        subtitle.Foreground(secondaryBrush);
        subtitle.FontSize(12);
        subtitle.TextWrapping(TextWrapping::Wrap);
        namePanel.Children().Append(subtitle);

        Grid::SetColumn(namePanel, 0);

        auto actionPanel = StackPanel();
        actionPanel.Orientation(Orientation::Horizontal);
        actionPanel.VerticalAlignment(VerticalAlignment::Center);
        actionPanel.Spacing(10);
        Grid::SetColumn(actionPanel, 1);

        auto toggle = ToggleSwitch();
        toggle.IsOn(globalAutoReconnect || dev.AutoReconnect);
        toggle.IsEnabled(!globalAutoReconnect);
        toggle.MinWidth(64);
        toggle.VerticalAlignment(VerticalAlignment::Center);
        toggle.OffContent(box_value(L""));
        toggle.OnContent(box_value(L""));
        if (globalAutoReconnect) {
            apc::ui::SetTooltipText(toggle, winrt::hstring(_("Device_AutoReconnect_Global")));
        } else {
            apc::ui::SetTooltipText(toggle, winrt::hstring(_("Device_AutoReconnect")));
        }
        auto weak = get_weak();
        toggle.Toggled([id = dev.Id, weak](auto const& s, auto) {
            if (auto self = weak.get()) {
                if (auto settingsController = self->m_settingsController) {
                    settingsController->SetDeviceAutoReconnect(id, s.template as<ToggleSwitch>().IsOn());
                }
            }
        });

        apc::ui::IconButtonOptions forgetOptions;
        forgetOptions.Width = 36;
        forgetOptions.Height = 32;
        forgetOptions.IconFontSize = 14;
        forgetOptions.Foreground = apc::ui::TryThemeBrush(L"SystemFillColorCriticalBrush");
        forgetOptions.TransparentBackground = false;
        forgetOptions.Borderless = false;
        auto forgetText = winrt::hstring(_("Device_Forget"));
        auto forgetBtn = apc::ui::CreateIconButton(L"\xE74D", forgetText, forgetOptions);
        forgetBtn.Click([id = dev.Id, weak](auto, auto) {
            if (auto self = weak.get()) {
                if (auto settingsController = self->m_settingsController) {
                    settingsController->ForgetDevice(id);
                }
                self->RebuildDeviceList();
            }
        });

        item.Children().Append(namePanel);
        actionPanel.Children().Append(toggle);
        actionPanel.Children().Append(forgetBtn);
        item.Children().Append(actionPanel);
        row.Child(item);
        DevicesPanel().Children().Append(row);
    }
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void SettingsWindow::SetSettingsController(std::shared_ptr<ISettingsController> controller) {
    m_settingsController = std::move(controller);
}

void SettingsWindow::SetDefaultPlacement(util::SettingsWindowPlacement placement) {
    m_defaultPlacement = placement;
}

void SettingsWindow::SetTargetPlacement(util::SettingsWindowPlacement placement) {
    m_targetPlacement = placement;
}

} // namespace winrt::AudioPlaybackConnector2::implementation
