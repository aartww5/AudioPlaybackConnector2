#include <pch.h>

#include <app/DeviceEventRouter.hpp>

#include <core/DeviceManager.hpp>

struct DeviceEventRouter::State {
    std::atomic<bool> active = true;
    UiDispatcher dispatcher;
    Callbacks callbacks;
};

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Constructors / Destructor /////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

DeviceEventRouter::~DeviceEventRouter() {
    Detach();
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DeviceEventRouter::Attach(std::shared_ptr<DeviceManager> deviceManager,
                               UiDispatcher dispatcher,
                               Callbacks callbacks) {
    Detach();
    if (!deviceManager) return;

    m_deviceManager = std::move(deviceManager);
    m_state = std::make_shared<State>();
    m_state->dispatcher = std::move(dispatcher);
    m_state->callbacks = std::move(callbacks);

    auto state = m_state;
    m_deviceConnectedToken = m_deviceManager->DeviceConnected += [state](winrt::hstring id) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceConnected received: {0}", std::wstring(id));
        Dispatch(state, [state, id = std::move(id)]() {
            if (!state->active.load() || !state->callbacks.DeviceConnected) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceConnected callback begin: {0}", std::wstring(id));
            state->callbacks.DeviceConnected(id);
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceConnected callback end: {0}", std::wstring(id));
        });
    };
    m_deviceDisconnectedToken = m_deviceManager->DeviceDisconnected += [state](winrt::hstring id) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceDisconnected received: {0}", std::wstring(id));
        Dispatch(state, [state, id = std::move(id)]() {
            if (!state->active.load() || !state->callbacks.DeviceDisconnected) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceDisconnected callback begin: {0}", std::wstring(id));
            state->callbacks.DeviceDisconnected(id);
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceDisconnected callback end: {0}", std::wstring(id));
        });
    };
    m_connectionErrorToken = m_deviceManager->ConnectionError += [state](winrt::hstring id, winrt::hstring msg) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] ConnectionError received: id={0} message={1}",
                             std::wstring(id),
                             std::wstring(msg));
        Dispatch(state, [state, id = std::move(id), msg = std::move(msg)]() {
            if (!state->active.load() || !state->callbacks.ConnectionError) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] ConnectionError callback begin: {0}", std::wstring(id));
            state->callbacks.ConnectionError(id, msg);
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] ConnectionError callback end: {0}", std::wstring(id));
        });
    };
    m_autoReconnectTriggeredToken = m_deviceManager->AutoReconnectTriggered += [state](winrt::hstring id) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] AutoReconnectTriggered received: {0}", std::wstring(id));
        Dispatch(state, [state, id = std::move(id)]() {
            if (!state->active.load() || !state->callbacks.AutoReconnectTriggered) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] AutoReconnectTriggered callback begin: {0}",
                                 std::wstring(id));
            state->callbacks.AutoReconnectTriggered(id);
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] AutoReconnectTriggered callback end: {0}",
                                 std::wstring(id));
        });
    };
    m_autoReconnectFailedToken = m_deviceManager->AutoReconnectFailed += [state](winrt::hstring id) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] AutoReconnectFailed received: {0}", std::wstring(id));
        Dispatch(state, [state, id = std::move(id)]() {
            if (!state->active.load() || !state->callbacks.AutoReconnectFailed) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] AutoReconnectFailed callback begin: {0}",
                                 std::wstring(id));
            state->callbacks.AutoReconnectFailed(id);
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] AutoReconnectFailed callback end: {0}", std::wstring(id));
        });
    };
    m_deviceStatusChangedToken = m_deviceManager->DeviceStatusChanged += [state](auto id, auto status, auto) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceStatusChanged received: id={0} status={1}",
                             std::wstring(id),
                             std::wstring(status));
        Dispatch(state, [state, id = std::move(id), status = std::move(status)]() {
            if (!state->active.load() || !state->callbacks.DeviceStatusChanged) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceStatusChanged callback begin: id={0} status={1}",
                                 std::wstring(id),
                                 std::wstring(status));
            state->callbacks.DeviceStatusChanged(id, status);
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceStatusChanged callback end: id={0}",
                                 std::wstring(id));
        });
    };
    m_deviceActivityChangedToken = m_deviceManager->DeviceActivityChanged += [state](auto id) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceActivityChanged received: {0}", std::wstring(id));
        Dispatch(state, [state]() {
            if (!state->active.load() || !state->callbacks.DeviceActivityChanged) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceActivityChanged callback begin");
            state->callbacks.DeviceActivityChanged();
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] DeviceActivityChanged callback end");
        });
    };
}

void DeviceEventRouter::Detach() noexcept {
    if (m_state) {
        m_state->active.store(false);
    }

    auto deviceManager = std::exchange(m_deviceManager, nullptr);
    if (deviceManager) {
        try {
            if (m_deviceConnectedToken) {
                deviceManager->DeviceConnected -= m_deviceConnectedToken;
            }
            if (m_deviceDisconnectedToken) {
                deviceManager->DeviceDisconnected -= m_deviceDisconnectedToken;
            }
            if (m_connectionErrorToken) {
                deviceManager->ConnectionError -= m_connectionErrorToken;
            }
            if (m_autoReconnectTriggeredToken) {
                deviceManager->AutoReconnectTriggered -= m_autoReconnectTriggeredToken;
            }
            if (m_autoReconnectFailedToken) {
                deviceManager->AutoReconnectFailed -= m_autoReconnectFailedToken;
            }
            if (m_deviceStatusChangedToken) {
                deviceManager->DeviceStatusChanged -= m_deviceStatusChangedToken;
            }
            if (m_deviceActivityChangedToken) {
                deviceManager->DeviceActivityChanged -= m_deviceActivityChangedToken;
            }
        } catch (...) {
            DebugTrace(L"[DeviceEventRouter] Detach ERROR: ignored exception");
        }
    }

    ResetTokens();
    m_state.reset();
}

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Helpers ///////////////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

void DeviceEventRouter::Dispatch(std::shared_ptr<State> const& state, std::function<void()> work) {
    if (!state || !state->active.load()) return;
    if (state->dispatcher) {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] Dispatch enqueue requested");
        state->dispatcher([state, work = std::move(work)]() mutable {
            if (!state->active.load()) return;
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] Dispatch work begin");
            work();
            DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] Dispatch work end");
        });
    } else {
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] Dispatch inline begin");
        work();
        DebugTraceDiagnostic(L"[Diag][DeviceEventRouter] Dispatch inline end");
    }
}

void DeviceEventRouter::ResetTokens() noexcept {
    m_deviceConnectedToken = 0;
    m_deviceDisconnectedToken = 0;
    m_connectionErrorToken = 0;
    m_autoReconnectTriggeredToken = 0;
    m_autoReconnectFailedToken = 0;
    m_deviceStatusChangedToken = 0;
    m_deviceActivityChangedToken = 0;
}
