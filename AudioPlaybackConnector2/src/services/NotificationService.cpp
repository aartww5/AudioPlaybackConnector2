#include <pch.h>
#include <services/NotificationService.hpp>
#include <core/StringResources.hpp>
#include <services/ToastContentBuilder.hpp>
#include <services/UpdateService.hpp>
#include <util/Util.hpp>

#include <utility>

namespace AppNotifications = winrt::Microsoft::Windows::AppNotifications;

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Helpers ///////////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

namespace {

constexpr wchar_t kStatusNotificationGroup[] = L"audioPlaybackConnectorStatus";
constexpr wchar_t kStatusNotificationTagPrefix[] = L"currentStatus:";

std::wstring NotificationText(std::string_view key, std::wstring_view replacement = {}) {
    return util::ReplacePlaceholders(std::wstring(_(key)), replacement);
}

winrt::Windows::Foundation::DateTime ExpirationFromNow(std::chrono::seconds seconds) {
    return winrt::clock::now() + seconds;
}

bool IsPackagedProcess() {
    UINT32 length = 0;
    const auto result = GetCurrentPackageFullName(&length, nullptr);
    return result != APPMODEL_ERROR_NO_PACKAGE;
}

} // namespace

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

NotificationService::~NotificationService() {
    Teardown();
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Lifecycle /////////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

bool NotificationService::Initialize(winrt::hstring const& appName, winrt::Windows::Foundation::Uri const& logoUri) {
    try {
        if (!AppNotifications::AppNotificationManager::IsSupported()) {
            DebugTrace(L"[NotificationService] AppNotificationManager is not supported; notifications disabled");
            return false;
        }

        {
            auto guard = m_lock.lock_exclusive();
            m_isTearingDown = false;
        }

        auto notificationManager = AppNotifications::AppNotificationManager::Default();
        auto weak = weak_from_this();
        auto notificationInvokedToken = notificationManager.NotificationInvoked([weak](auto const&, auto const& args) {
            if (auto self = weak.lock()) {
                self->OnNotificationInvoked(args);
            }
        });

        if (IsPackagedProcess()) {
            notificationManager.Register();
        } else {
            notificationManager.Register(appName, logoUri);
        }

        {
            auto guard = m_lock.lock_exclusive();
            m_notificationManager = notificationManager;
            m_notificationInvokedToken = notificationInvokedToken;
            m_notificationsRegistered = true;
        }
        DebugTrace(L"[NotificationService] AppNotificationManager registered");
        return true;
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[NotificationService] AppNotificationManager registration failed", ex);
        Teardown();
        return false;
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[NotificationService] AppNotificationManager registration failed", ex);
        Teardown();
        return false;
    } catch (...) {
        util::DebugTraceUnknownException(L"[NotificationService] AppNotificationManager registration failed");
        Teardown();
        return false;
    }
}

void NotificationService::Teardown() noexcept {
    AppNotifications::AppNotificationManager notificationManager{nullptr};
    winrt::event_token notificationInvokedToken{};
    bool notificationsRegistered = false;

    {
        auto guard = m_lock.lock_exclusive();
        m_isTearingDown = true;
        notificationManager = std::exchange(m_notificationManager, nullptr);
        notificationInvokedToken = std::exchange(m_notificationInvokedToken, {});
        notificationsRegistered = std::exchange(m_notificationsRegistered, false);
        m_reconnectCallback = nullptr;
        m_shouldShowNotificationCallback = nullptr;
    }

    try {
        if (notificationManager && notificationInvokedToken.value) {
            notificationManager.NotificationInvoked(notificationInvokedToken);
        }
        if (notificationManager && notificationsRegistered) {
            notificationManager.Unregister();
        }
    } catch (...) {
    }
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Callbacks /////////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void NotificationService::SetReconnectCallback(ReconnectRequestedCallback callback) {
    auto guard = m_lock.lock_exclusive();
    m_reconnectCallback = std::move(callback);
}

void NotificationService::SetShouldShowNotificationCallback(ShouldShowNotificationCallback callback) {
    auto guard = m_lock.lock_exclusive();
    m_shouldShowNotificationCallback = std::move(callback);
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Internal Helpers //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

NotificationService::StatusNotificationTagReservation NotificationService::ReserveStatusNotificationTag() {
    auto guard = m_lock.lock_exclusive();
    const auto generation = ++m_statusNotificationGeneration;

    StatusNotificationTagReservation reservation;
    reservation.TagsToRemove = std::move(m_statusNotificationTags);
    reservation.CurrentTag = winrt::hstring(kStatusNotificationTagPrefix) + winrt::hstring(std::to_wstring(generation));
    reservation.Generation = generation;

    m_statusNotificationTags.clear();
    m_statusNotificationTags.push_back(reservation.CurrentTag);
    return reservation;
}

bool NotificationService::IsStatusNotificationGenerationCurrent(uint64_t generation) const {
    auto guard = m_lock.lock_shared();
    return !m_isTearingDown && generation == m_statusNotificationGeneration;
}

bool NotificationService::ShouldShowNotifications() const {
    ShouldShowNotificationCallback callback;
    {
        auto guard = m_lock.lock_shared();
        callback = m_shouldShowNotificationCallback;
    }
    return !callback || callback();
}

winrt::fire_and_forget NotificationService::ShowToastAsync(std::wstring xml,
                                                           winrt::hstring group,
                                                           winrt::hstring tag,
                                                           // cppcheck-suppress passedByValue
                                                           std::vector<winrt::hstring> tagsToRemove,
                                                           uint64_t generation,
                                                           winrt::Windows::Foundation::DateTime expiration) {
    auto lifetime = shared_from_this();
    AppNotifications::AppNotificationManager notificationManager{nullptr};
    {
        auto guard = m_lock.lock_shared();
        if (m_isTearingDown || !m_notificationManager || !m_notificationsRegistered) {
            co_return;
        }
        notificationManager = m_notificationManager;
    }

    try {
        for (auto const& tagToRemove : tagsToRemove) {
            if (!tagToRemove.empty() && tagToRemove != tag) {
                co_await notificationManager.RemoveByTagAndGroupAsync(tagToRemove, group);
            }
        }

        if (!IsStatusNotificationGenerationCurrent(generation)) co_return;

        AppNotifications::AppNotification notification{winrt::hstring(xml)};
        notification.Group(group);
        notification.Tag(tag);
        notification.Expiration(expiration);
        notification.ExpiresOnReboot(true);
        notificationManager.Show(notification);
        co_return;
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[NotificationService] AppNotificationManager.Show failed", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[NotificationService] AppNotificationManager.Show failed", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[NotificationService] AppNotificationManager.Show failed");
    }
}

bool NotificationService::ShowStatusToast(std::wstring const& xml,
                                          winrt::Windows::Foundation::DateTime const& expiration) {
    {
        auto guard = m_lock.lock_shared();
        if (m_isTearingDown || !m_notificationManager || !m_notificationsRegistered) {
            return false;
        }
    }

    auto reservation = ReserveStatusNotificationTag();
    ShowToastAsync(std::wstring(xml),
                   kStatusNotificationGroup,
                   reservation.CurrentTag,
                   std::move(reservation.TagsToRemove),
                   reservation.Generation,
                   expiration);
    return true;
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Show Notifications ////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void NotificationService::ShowAppStarted() {
    if (!ShouldShowNotifications()) return;
    auto title = NotificationText("Notification_AppStarted_Title");
    auto body = NotificationText("Notification_AppStarted_Body");
    auto xml = ToastXmlBuilder{}
                   .Title(title)
                   .Body(body)
                   .AppLogoOverride(L"ms-appx:///Images/ToastInfo.png")
                   .SilentAudio()
                   .Build();
    ShowStatusToast(xml, ExpirationFromNow(std::chrono::seconds(7)));
}

void NotificationService::ShowDeviceConnected(winrt::hstring const& id, winrt::hstring const& deviceName) {
    if (!ShouldShowNotifications()) return;
    auto title = NotificationText("Notification_Connected", deviceName);
    auto xml = ToastXmlBuilder{}
                   .Title(title)
                   .Caption(NotificationText("Notification_Connected_Caption"))
                   .Action(NotificationText("Reconnect"), ToastArguments{}.Action(L"reconnect").DeviceId(id))
                   .AppLogoOverride(L"ms-appx:///Images/ToastConnected.png")
                   .Audio(L"ms-winsoundevent:Notification.Default")
                   .Duration(L"long")
                   .Build();

    ShowStatusToast(xml, ExpirationFromNow(std::chrono::minutes(1)));
}

void NotificationService::ShowDeviceDisconnected(winrt::hstring const&, winrt::hstring const& deviceName) {
    if (!ShouldShowNotifications()) return;
    auto title = NotificationText("Notification_Disconnected", deviceName);
    auto xml = ToastXmlBuilder{}
                   .Title(title)
                   .Body(NotificationText("Notification_Disconnected_Body"))
                   .AppLogoOverride(L"ms-appx:///Images/ToastWarning.png")
                   .SilentAudio()
                   .Build();

    ShowStatusToast(xml, ExpirationFromNow(std::chrono::minutes(1)));
}

void NotificationService::ShowAutoReconnect(winrt::hstring const&, winrt::hstring const& deviceName) {
    if (!ShouldShowNotifications()) return;
    auto title = NotificationText("Notification_AutoReconnect", deviceName);
    auto xml = ToastXmlBuilder{}
                   .Title(title)
                   .Body(NotificationText("Notification_AutoReconnect_Body"))
                   .AppLogoOverride(L"ms-appx:///Images/ToastReconnect.png")
                   .SilentAudio()
                   .Build();

    ShowStatusToast(xml, ExpirationFromNow(std::chrono::minutes(1)));
}

void NotificationService::ShowAutoReconnectFailed(winrt::hstring const& id, winrt::hstring const& deviceName) {
    if (!ShouldShowNotifications()) return;
    auto title = NotificationText("Notification_AutoReconnectFailed_Title", deviceName);
    auto xml = ToastXmlBuilder{}
                   .Title(title)
                   .Body(NotificationText("Notification_AutoReconnectFailed_Body"))
                   .Action(NotificationText("Notification_Retry"), ToastArguments{}.Action(L"retry").DeviceId(id))
                   .AppLogoOverride(L"ms-appx:///Images/ToastError.png")
                   .Audio(L"ms-winsoundevent:Notification.Looping.Alarm2")
                   .Build();

    ShowStatusToast(xml, ExpirationFromNow(std::chrono::hours(1)));
}

bool NotificationService::ShowUpdateAvailable(std::wstring const& latestVersion) {
    if (!ShouldShowNotifications()) return false;
    auto title = NotificationText("Notification_UpdateAvailable_Title", latestVersion);
    auto body = NotificationText("Notification_UpdateAvailable_Body");
    auto xml =
        ToastXmlBuilder{}
            .Title(title)
            .Body(body)
            .Action(NotificationText("Notification_UpdateAvailable_Action"), ToastArguments{}.Action(L"openUpdate"))
            .AppLogoOverride(L"ms-appx:///Images/ToastInfo.png")
            .SilentAudio()
            .Build();

    return ShowStatusToast(xml, ExpirationFromNow(std::chrono::hours(6)));
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Event Handler /////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void NotificationService::OnNotificationInvoked(AppNotifications::AppNotificationActivatedEventArgs const& args) {
    try {
        ReconnectRequestedCallback reconnectCallback;
        {
            auto guard = m_lock.lock_shared();
            if (m_isTearingDown) return;
            reconnectCallback = m_reconnectCallback;
        }

        auto parsedArguments = ToastArguments::Parse(args.Argument());
        auto action = ToastArguments::Find(parsedArguments, L"action");
        auto deviceId = ToastArguments::Find(parsedArguments, L"deviceId");

        if (action && *action == L"openUpdate") {
            DebugTrace(L"[NotificationService] App notification invoked: action=openUpdate");
            UpdateService::LaunchAppInstallerAsync();
            return;
        }

        if (!deviceId) {
            DebugTrace(L"[NotificationService] App notification invoked without deviceId: {0}",
                       std::wstring(args.Argument()));
            return;
        }

        DebugTrace(L"[NotificationService] App notification invoked: action={0}, deviceId={1}",
                   action.value_or(L""),
                   *deviceId);

        if (action && (*action == L"reconnect" || *action == L"retry") && reconnectCallback) {
            reconnectCallback(winrt::hstring(*deviceId));
        }
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[NotificationService] App notification activation failed", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[NotificationService] App notification activation failed", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[NotificationService] App notification activation failed");
    }
}
