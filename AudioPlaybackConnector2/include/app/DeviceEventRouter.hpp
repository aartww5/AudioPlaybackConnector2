#pragma once

#include <functional>
#include <memory>

class DeviceManager;
enum class DeviceStatusKind;

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Device Event Router ///////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

class DeviceEventRouter {
public:
    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Type Aliases //////////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    using UiDispatcher = std::function<void(std::function<void()> work)>;

    struct Callbacks {
        std::function<void(winrt::hstring const& id)> DeviceConnected;
        std::function<void(winrt::hstring const& id)> DeviceDisconnected;
        std::function<void(winrt::hstring const& id, winrt::hstring const& message)> ConnectionError;
        std::function<void(winrt::hstring const& id)> AutoReconnectTriggered;
        std::function<void(winrt::hstring const& id)> AutoReconnectFailed;
        std::function<void(winrt::hstring const& id, winrt::hstring const& status, DeviceStatusKind statusKind)>
            DeviceStatusChanged;
        std::function<void()> DeviceActivityChanged;
    };

    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    DeviceEventRouter() = default;
    ~DeviceEventRouter();

    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    void Attach(std::shared_ptr<DeviceManager> deviceManager, UiDispatcher dispatcher, Callbacks callbacks);
    void Detach() noexcept;

private:
    struct State;

    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Helpers ///////////////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    static void Dispatch(std::shared_ptr<State> const& state, std::function<void()> work);
    void ResetTokens() noexcept;

    /*------------------------------------------------------------------------------------------------------------*/
    /*//////// Member Variables //////////////////////////////////////////////////////////////////////////////////*/
    /*------------------------------------------------------------------------------------------------------------*/

    std::shared_ptr<DeviceManager> m_deviceManager;
    std::shared_ptr<State> m_state;

    std::size_t m_deviceConnectedToken = 0;
    std::size_t m_deviceDisconnectedToken = 0;
    std::size_t m_connectionErrorToken = 0;
    std::size_t m_autoReconnectTriggeredToken = 0;
    std::size_t m_autoReconnectFailedToken = 0;
    std::size_t m_deviceStatusChangedToken = 0;
    std::size_t m_deviceActivityChangedToken = 0;
};
