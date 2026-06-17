#include <pch.h>

#include <services/StartupTaskController.hpp>

namespace {
constexpr wchar_t c_startupTaskId[] = L"AudioPlaybackConnector2StartupTask";
}

bool StartupTaskController::IsEnabledState(winrt::Windows::ApplicationModel::StartupTaskState state) noexcept {
    return state == winrt::Windows::ApplicationModel::StartupTaskState::Enabled ||
           state == winrt::Windows::ApplicationModel::StartupTaskState::EnabledByPolicy;
}

winrt::Windows::Foundation::IAsyncOperation<bool> StartupTaskController::IsEnabledAsync() {
    auto task = co_await winrt::Windows::ApplicationModel::StartupTask::GetAsync(c_startupTaskId);
    co_return IsEnabledState(task.State());
}

winrt::Windows::Foundation::IAsyncOperation<bool> StartupTaskController::SetEnabledAsync(bool enabled) {
    auto task = co_await winrt::Windows::ApplicationModel::StartupTask::GetAsync(c_startupTaskId);
    if (enabled == IsEnabledState(task.State())) {
        co_return true;
    }

    if (!enabled) {
        task.Disable();
        co_return true;
    }

    auto state = co_await task.RequestEnableAsync();
    co_return IsEnabledState(state);
}
