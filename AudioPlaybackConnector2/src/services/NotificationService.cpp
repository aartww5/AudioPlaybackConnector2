#include <pch.h>
#include <services/NotificationService.hpp>
#include <core/StringResources.hpp>
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

std::wstring_view AsView(winrt::hstring const& value) {
    return std::wstring_view(value.c_str(), value.size());
}

std::wstring XmlEscape(std::wstring_view value) {
    std::wstring result;
    result.reserve(value.size());
    for (auto ch : value) {
        switch (ch) {
            case L'&': result += L"&amp;"; break;
            case L'<': result += L"&lt;"; break;
            case L'>': result += L"&gt;"; break;
            case L'"': result += L"&quot;"; break;
            case L'\'': result += L"&apos;"; break;
            default: result += ch; break;
        }
    }
    return result;
}

std::wstring UrlDecodeComponent(std::wstring_view value) {
    std::wstring normalized(value);
    std::replace(normalized.begin(), normalized.end(), L'+', L' ');

    try {
        return std::wstring(winrt::Windows::Foundation::Uri::UnescapeComponent(winrt::hstring(normalized)));
    } catch (...) {
        return normalized;
    }
}

class ToastArguments final {
public:
    ToastArguments& Add(std::wstring_view key, std::wstring_view value) {
        if (!m_value.empty()) {
            m_value += L'&';
        }
        m_value += EscapeComponent(key);
        m_value += L'=';
        m_value += EscapeComponent(value);
        return *this;
    }

    ToastArguments& Action(std::wstring_view action) { return Add(L"action", action); }

    ToastArguments& DeviceId(winrt::hstring const& deviceId) {
        if (!deviceId.empty()) {
            Add(L"deviceId", AsView(deviceId));
        }
        return *this;
    }

    [[nodiscard]] winrt::hstring ToHString() const { return winrt::hstring(m_value); }

    [[nodiscard]] static std::unordered_map<std::wstring, std::wstring> Parse(winrt::hstring const& rawArguments) {
        std::unordered_map<std::wstring, std::wstring> result;
        std::wstring_view raw(rawArguments.c_str(), rawArguments.size());

        size_t start = 0;
        while (start <= raw.size()) {
            const auto separator = raw.find(L'&', start);
            const auto end = separator == std::wstring_view::npos ? raw.size() : separator;
            const auto pair = raw.substr(start, end - start);

            if (!pair.empty()) {
                const auto equals = pair.find(L'=');
                const auto key = equals == std::wstring_view::npos ? pair : pair.substr(0, equals);
                const auto value = equals == std::wstring_view::npos ? std::wstring_view{} : pair.substr(equals + 1);
                auto decodedKey = UrlDecodeComponent(key);
                if (!decodedKey.empty()) {
                    result[std::move(decodedKey)] = UrlDecodeComponent(value);
                }
            }

            if (separator == std::wstring_view::npos) break;
            start = separator + 1;
        }

        return result;
    }

    [[nodiscard]] static std::optional<std::wstring>
    Find(std::unordered_map<std::wstring, std::wstring> const& arguments, std::wstring_view key) {
        auto it = arguments.find(std::wstring(key));
        if (it == arguments.end() || it->second.empty()) return std::nullopt;
        return it->second;
    }

private:
    [[nodiscard]] static std::wstring EscapeComponent(std::wstring_view value) {
        return std::wstring(winrt::Windows::Foundation::Uri::EscapeComponent(winrt::hstring(std::wstring(value))));
    }

    std::wstring m_value;
};

winrt::Windows::Foundation::DateTime ExpirationFromNow(std::chrono::seconds seconds) {
    return winrt::clock::now() + seconds;
}

bool IsPackagedProcess() {
    UINT32 length = 0;
    const auto result = GetCurrentPackageFullName(&length, nullptr);
    return result != APPMODEL_ERROR_NO_PACKAGE;
}

class ToastXmlBuilder final {
public:
    ToastXmlBuilder& Title(std::wstring_view value) {
        m_title = value;
        return *this;
    }

    ToastXmlBuilder& Body(std::wstring_view value) {
        m_body = value;
        return *this;
    }

    ToastXmlBuilder& Caption(std::wstring_view value) {
        m_caption = value;
        return *this;
    }

    ToastXmlBuilder& Action(std::wstring_view text, ToastArguments const& arguments) {
        m_actionText = text;
        m_actionArguments = arguments.ToHString();
        return *this;
    }

    ToastXmlBuilder& AppLogoOverride(std::wstring_view value) {
        m_appLogoOverride = value;
        return *this;
    }

    ToastXmlBuilder& Audio(std::wstring_view src) {
        m_audioSrc = src;
        m_silentAudio = false;
        return *this;
    }

    ToastXmlBuilder& SilentAudio() {
        m_audioSrc.clear();
        m_silentAudio = true;
        return *this;
    }

    ToastXmlBuilder& Duration(std::wstring_view value) {
        m_duration = value;
        return *this;
    }

    [[nodiscard]] std::wstring Build() const {
        std::wstring xml = L"<toast";
        if (!m_duration.empty()) {
            xml += L" duration=\"";
            xml += XmlEscape(m_duration);
            xml += L"\"";
        }
        xml += L"><visual><binding template=\"ToastGeneric\">";
        if (!m_appLogoOverride.empty()) {
            xml += L"<image placement=\"appLogoOverride\" hint-crop=\"circle\" src=\"";
            xml += XmlEscape(m_appLogoOverride);
            xml += L"\"/>";
        }
        AppendText(xml, m_title);
        if (!m_body.empty()) {
            AppendText(xml, m_body);
        }
        if (!m_caption.empty()) {
            AppendText(xml, m_caption, L" hint-style=\"caption\" hint-color=\"secondary\"");
        }
        xml += L"</binding></visual>";
        if (!m_actionText.empty()) {
            xml += L"<actions><action content=\"";
            xml += XmlEscape(m_actionText);
            xml += L"\" arguments=\"";
            xml += XmlEscape(AsView(m_actionArguments));
            xml += L"\"/></actions>";
        }
        if (m_silentAudio) {
            xml += L"<audio silent=\"true\"/>";
        } else if (!m_audioSrc.empty()) {
            xml += L"<audio src=\"";
            xml += XmlEscape(m_audioSrc);
            xml += L"\"/>";
        }
        xml += L"</toast>";
        return xml;
    }

private:
    static void AppendText(std::wstring& xml, std::wstring_view text, std::wstring_view attributes = {}) {
        xml += L"<text";
        xml += attributes;
        xml += L">";
        xml += XmlEscape(text);
        xml += L"</text>";
    }

    std::wstring m_title;
    std::wstring m_body;
    std::wstring m_caption;
    std::wstring m_actionText;
    winrt::hstring m_actionArguments;
    std::wstring m_appLogoOverride;
    std::wstring m_audioSrc;
    std::wstring m_duration;
    bool m_silentAudio = false;
};

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
