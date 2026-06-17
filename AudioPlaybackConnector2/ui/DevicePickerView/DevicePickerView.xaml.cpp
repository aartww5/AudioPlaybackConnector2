#include <pch.h>
#include <ui/DevicePickerView/DevicePickerView.xaml.h>
#if __has_include("DevicePickerView.g.cpp")
#include <DevicePickerView.g.cpp>
#endif

#include <core/DeviceManager.hpp>
#include <core/StringResources.hpp>
#include <util/Util.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace {
void CancelRefreshDevicesOperation(winrt::Windows::Foundation::IAsyncOperation<
                                       winrt::Windows::Devices::Enumeration::DeviceInformationCollection> const& op,
                                   std::wstring_view context) noexcept {
    if (!op) return;

    try {
        op.Cancel();
    } catch (winrt::hresult_error const& ex) {
        DebugTrace(L"[DevicePickerView] ERROR: {0} failed to cancel RefreshDevicesAsync: 0x{1:08X} {2}",
                   context,
                   static_cast<uint32_t>(ex.code()),
                   ex.message());
    } catch (std::exception const& ex) {
        DebugTrace(L"[DevicePickerView] ERROR: {0} failed to cancel RefreshDevicesAsync: {1}",
                   context,
                   util::Utf8ToUtf16(ex.what()));
    } catch (...) {
        DebugTrace(L"[DevicePickerView] ERROR: {0} failed to cancel RefreshDevicesAsync: unknown exception", context);
    }
}
} // namespace

namespace winrt::AudioPlaybackConnector2::implementation {
/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

DevicePickerView::DevicePickerView() {
    InitializeComponent();
    BackdropElement().SystemBackdrop(Media::MicaBackdrop());
    auto weak = get_weak();
    CloseButton().Click([weak](auto const& sender, auto const& args) {
        if (auto self = weak.get()) {
            self->OnCloseClicked(sender, args);
        }
    });
    DisconnectAllButton().Click([weak](auto const& sender, auto const& args) {
        if (auto self = weak.get()) {
            self->OnDisconnectAllClicked(sender, args);
        }
    });
    ReconnectAllButton().Click([weak](auto const& sender, auto const& args) {
        if (auto self = weak.get()) {
            self->OnReconnectAllClicked(sender, args);
        }
    });
    DeviceList().SelectionChanged([weak](auto const& sender, auto const& args) {
        if (auto self = weak.get()) {
            self->OnDeviceSelected(sender, args);
        }
    });
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DevicePickerView::Initialize(std::shared_ptr<DeviceManager> manager,
                                  std::function<void()> onClose,
                                  std::function<void(winrt::hstring)> onDeviceSelected,
                                  std::function<void(winrt::hstring)> onDeviceDisconnect,
                                  std::function<void(winrt::hstring)> onDeviceReconnect,
                                  std::function<void()> onDisconnectAll,
                                  std::function<void()> onReconnectAll) {
    m_deviceManager = manager;
    m_viewModel.SetDeviceManager(manager);
    m_onClose = std::move(onClose);
    m_onDeviceSelected = std::move(onDeviceSelected);
    m_onDeviceDisconnect = std::move(onDeviceDisconnect);
    m_onDeviceReconnect = std::move(onDeviceReconnect);
    m_onDisconnectAll = std::move(onDisconnectAll);
    m_onReconnectAll = std::move(onReconnectAll);
    TitleText().Text(winrt::hstring(_("TrayMenu_SelectDevice")));
    DisconnectAllText().Text(winrt::hstring(_("DisconnectAll")));
    ReconnectAllText().Text(winrt::hstring(_("ReconnectAll")));
}

void DevicePickerView::LoadDevices() {
    if (m_isLoadingDevices) return;

    m_isLoadingDevices.store(true);
    m_loadDevicesCancelled.store(false);
    auto requestId = ++m_loadDevicesRequestId;
    m_activeLoadRequestId.store(requestId);

    bool listWasEmpty = DeviceList().Items().Size() == 0;
    if (listWasEmpty) {
        ProgressIndicator().Visibility(Visibility::Visible);
        ProgressIndicator().IsActive(true);
    }

    auto dispatcher = this->DispatcherQueue();
    if (!dispatcher) {
        DebugTrace(L"[DevicePickerView] ERROR: no UI dispatcher available for LoadDevices");
        m_isLoadingDevices.store(false);
        m_activeLoadRequestId.store(0);
        return;
    }

    auto manager = m_deviceManager.lock();
    if (!manager) {
        DebugTrace(L"[DevicePickerView] ERROR: no DeviceManager available for LoadDevices");
        OnDeviceEnumerationFailed(listWasEmpty, requestId);
        return;
    }

    auto weak = get_weak();

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Devices::Enumeration::DeviceInformationCollection>
        previousOp{nullptr};
    {
        std::lock_guard lock(m_refreshDevicesOpMutex);
        previousOp = std::exchange(m_refreshDevicesOp, nullptr);
    }
    CancelRefreshDevicesOperation(previousOp, L"LoadDevices");

    auto refreshOp = manager->RefreshDevicesAsync();
    {
        std::lock_guard lock(m_refreshDevicesOpMutex);
        m_refreshDevicesOp = refreshOp;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Devices::Enumeration::DeviceInformationCollection>
        pendingOp = refreshOp;

    pendingOp.Completed([weak, dispatcher, listWasEmpty, requestId](auto const& sender,
                                                                    winrt::Windows::Foundation::AsyncStatus status) {
        if (auto self = weak.get()) {
            std::lock_guard lock(self->m_refreshDevicesOpMutex);
            if (self->m_activeLoadRequestId.load() != requestId) {
                return;
            }
            self->m_refreshDevicesOp = nullptr;
        }

        if (status == winrt::Windows::Foundation::AsyncStatus::Canceled) {
            if (auto self = weak.get()) {
                if (self->m_loadDevicesRequestId.load() == requestId) {
                    self->m_isLoadingDevices.store(false);
                    self->m_activeLoadRequestId.store(0);
                }
            }
            return;
        }
        auto enqueueFailure = [&]() {
            bool enqueued = dispatcher.TryEnqueue([weak, listWasEmpty, requestId]() {
                if (auto self = weak.get()) self->OnDeviceEnumerationFailed(listWasEmpty, requestId);
            });
            if (!enqueued) {
                DebugTrace(L"[DevicePickerView] ERROR: failed to marshal OnDeviceEnumerationFailed to UI thread");
                if (auto self = weak.get()) {
                    if (self->m_loadDevicesRequestId.load() == requestId) {
                        ++self->m_loadDevicesRequestId;
                        self->m_isLoadingDevices.store(false);
                        self->m_activeLoadRequestId.store(0);
                    }
                }
            }
        };
        try {
            auto devices = sender.GetResults();
            bool enqueued = dispatcher.TryEnqueue([weak, devices, listWasEmpty, requestId]() {
                if (auto self = weak.get()) self->ApplyDeviceResults(devices, listWasEmpty, requestId);
            });
            if (!enqueued) {
                DebugTrace(L"[DevicePickerView] ERROR: failed to marshal ApplyDeviceResults to UI thread");
                if (auto self = weak.get()) {
                    if (self->m_loadDevicesRequestId.load() == requestId) {
                        ++self->m_loadDevicesRequestId;
                        self->m_isLoadingDevices.store(false);
                        self->m_activeLoadRequestId.store(0);
                    }
                }
            }
        } catch (winrt::hresult_error const& ex) {
            DebugTrace(L"[DevicePickerView] ERROR: RefreshDevicesAsync failed: 0x{0:08X} {1}",
                       static_cast<uint32_t>(ex.code()),
                       ex.message());
            enqueueFailure();
        } catch (std::exception const& ex) {
            DebugTrace(L"[DevicePickerView] ERROR: RefreshDevicesAsync failed: {0}", util::Utf8ToUtf16(ex.what()));
            enqueueFailure();
        } catch (...) {
            DebugTrace(L"[DevicePickerView] ERROR: RefreshDevicesAsync failed: unknown exception");
            enqueueFailure();
        }
    });
}

void DevicePickerView::CancelLoadDevices() {
    m_loadDevicesCancelled.store(true);
    auto invalidatedRequest = ++m_loadDevicesRequestId;
    m_activeLoadRequestId.store(invalidatedRequest);
    m_isLoadingDevices.store(false);
    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Devices::Enumeration::DeviceInformationCollection> op{
        nullptr};
    {
        std::lock_guard lock(m_refreshDevicesOpMutex);
        op = std::exchange(m_refreshDevicesOp, nullptr);
    }
    CancelRefreshDevicesOperation(op, L"CancelLoadDevices");
}

void DevicePickerView::RefreshDeviceStates() {
    if (m_isLoadingDevices.load()) {
        return;
    }
    RebuildDeviceListFromCache();
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Private Helpers ///////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DevicePickerView::ApplyDeviceResults(
    winrt::Windows::Devices::Enumeration::DeviceInformationCollection const& devices,
    bool listWasEmpty,
    uint64_t requestId) {
    if (m_loadDevicesRequestId.load() != requestId) return;
    if (m_activeLoadRequestId.load() != requestId) return;
    if (m_loadDevicesCancelled) {
        if (listWasEmpty) {
            ProgressIndicator().IsActive(false);
            ProgressIndicator().Visibility(Visibility::Collapsed);
        }
        m_isLoadingDevices.store(false);
        m_activeLoadRequestId.store(0);
        return;
    }
    ProgressIndicator().IsActive(false);
    ProgressIndicator().Visibility(Visibility::Collapsed);

    if (!devices) {
        m_isLoadingDevices.store(false);
        m_activeLoadRequestId.store(0);
        return;
    }

    m_viewModel.SetDevices(devices);
    RebuildDeviceListFromCache();
    m_isLoadingDevices.store(false);
    m_activeLoadRequestId.store(0);
}

void DevicePickerView::OnDeviceEnumerationFailed(bool listWasEmpty, uint64_t requestId) {
    if (m_loadDevicesRequestId.load() != requestId) return;
    if (m_activeLoadRequestId.load() != requestId) return;
    if (m_loadDevicesCancelled.load()) return;
    if (listWasEmpty) {
        ProgressIndicator().IsActive(false);
        ProgressIndicator().Visibility(Visibility::Collapsed);
    }
    m_isLoadingDevices.store(false);
    m_activeLoadRequestId.store(0);
}

void DevicePickerView::RebuildDeviceListFromCache() {
    m_suppressSelectionChanged.store(true);
    DeviceList().SelectedItem(nullptr);
    DeviceList().Items().Clear();

    int connectedCount = 0;
    if (auto manager = m_deviceManager.lock()) {
        connectedCount = static_cast<int>(manager->GetConnectedDevices().size());
    }
    GlobalActionsPanel().Visibility(connectedCount > 1 ? Visibility::Visible : Visibility::Collapsed);

    if (m_viewModel.Empty()) {
        auto emptyMsg = TextBlock();
        emptyMsg.Text(winrt::hstring(_("TrayMenu_NoDevices")));
        auto brush = Application::Current().Resources().TryLookup(box_value(L"TextFillColorSecondaryBrush"));
        if (brush) {
            emptyMsg.Foreground(brush.as<Media::SolidColorBrush>());
        } else {
            emptyMsg.Foreground(Media::SolidColorBrush(winrt::Windows::UI::Colors::Gray()));
        }
        DeviceList().Items().Append(emptyMsg);
    } else {
        auto items = m_viewModel.SnapshotItems();
        for (auto const& device : items) {
            DeviceList().Items().Append(BuildDeviceListItem(device));
        }
    }

    DeviceList().SelectedItem(nullptr);
    m_suppressSelectionChanged.store(false);
}

ListViewItem DevicePickerView::BuildDeviceListItem(DevicePickerItemViewModel const& device) {
    auto item = ListViewItem();
    auto grid = Grid();
    grid.ColumnDefinitions().Append(ColumnDefinition());
    grid.ColumnDefinitions().Append(ColumnDefinition());

    auto nameTb = TextBlock();
    nameTb.Text(device.Name);
    nameTb.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(nameTb, 0);

    auto infoPanel = StackPanel();
    infoPanel.Orientation(Orientation::Horizontal);
    infoPanel.HorizontalAlignment(HorizontalAlignment::Right);
    infoPanel.VerticalAlignment(VerticalAlignment::Center);
    infoPanel.Spacing(6);
    Grid::SetColumn(infoPanel, 1);

    if (device.IsBusy) {
        item.IsEnabled(false);
        item.Opacity(0.6);

        auto busyRing = ProgressRing();
        busyRing.Width(14);
        busyRing.Height(14);
        busyRing.IsActive(true);
        busyRing.VerticalAlignment(VerticalAlignment::Center);
        infoPanel.Children().Append(busyRing);
    }

    if (device.IsConnected) {
        auto devId = device.Id;
        auto onReconnect = m_onDeviceReconnect;
        auto onDisconnect = m_onDeviceDisconnect;

        auto reconnectBtn = Button();
        reconnectBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        reconnectBtn.BorderThickness({0});
        reconnectBtn.Padding({5, 1, 5, 1});
        reconnectBtn.VerticalAlignment(VerticalAlignment::Center);
        reconnectBtn.IsEnabled(!device.IsBusy);

        auto reconnectText = TextBlock();
        reconnectText.Text(winrt::hstring(_("Reconnect")));
        reconnectText.FontSize(11);
        if (auto brush = Application::Current().Resources().TryLookup(box_value(L"AccentFillColorDefaultBrush"))) {
            reconnectText.Foreground(brush.as<Media::Brush>());
        }
        reconnectBtn.Content(reconnectText);
        reconnectBtn.Click([onReconnect, devId](auto const&, auto const&) {
            if (onReconnect) onReconnect(devId);
        });

        auto disconnectBtn = Button();
        disconnectBtn.Background(Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        disconnectBtn.BorderThickness({0});
        disconnectBtn.Padding({5, 1, 5, 1});
        disconnectBtn.VerticalAlignment(VerticalAlignment::Center);
        disconnectBtn.IsEnabled(!device.IsBusy);

        auto disconnectText = TextBlock();
        disconnectText.Text(winrt::hstring(_("Disconnect")));
        disconnectText.FontSize(11);
        if (auto brush = Application::Current().Resources().TryLookup(box_value(L"SystemFillColorCriticalBrush"))) {
            disconnectText.Foreground(brush.as<Media::Brush>());
        }
        disconnectBtn.Content(disconnectText);
        disconnectBtn.Click([onDisconnect, devId](auto const&, auto const&) {
            if (onDisconnect) onDisconnect(devId);
        });

        infoPanel.Children().Append(reconnectBtn);
        infoPanel.Children().Append(disconnectBtn);
    }

    grid.Children().Append(nameTb);
    grid.Children().Append(infoPanel);
    item.Content(grid);
    item.Tag(box_value(device.Id));
    return item;
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Event Handlers ////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DevicePickerView::OnCloseClicked(winrt::Windows::Foundation::IInspectable const&,
                                      winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
    m_loadDevicesCancelled.store(true);
    if (m_onClose) m_onClose();
}

void DevicePickerView::OnDeviceSelected(winrt::Windows::Foundation::IInspectable const&,
                                        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    if (m_suppressSelectionChanged.load()) return;
    auto selected = DeviceList().SelectedItem();
    if (!selected) return;

    auto lvi = selected.try_as<ListViewItem>();
    if (!lvi) return;

    auto tag = lvi.Tag();
    if (!tag) return;
    auto id = unbox_value<winrt::hstring>(tag);
    if (id.empty()) return;

    if (!m_viewModel.CanSelect(id)) return;

    if (m_onDeviceSelected) {
        m_onDeviceSelected(id);
    }
}

void DevicePickerView::OnDisconnectAllClicked(winrt::Windows::Foundation::IInspectable const&,
                                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (m_onDisconnectAll) m_onDisconnectAll();
}

void DevicePickerView::OnReconnectAllClicked(winrt::Windows::Foundation::IInspectable const&,
                                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (m_onReconnectAll) m_onReconnectAll();
}
} // namespace winrt::AudioPlaybackConnector2::implementation
