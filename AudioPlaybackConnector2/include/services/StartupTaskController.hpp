#pragma once

class StartupTaskController final {
public:
    [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<bool> IsEnabledAsync();
    [[nodiscard]] static winrt::Windows::Foundation::IAsyncOperation<bool> SetEnabledAsync(bool enabled);

private:
    [[nodiscard]] static bool IsEnabledState(winrt::Windows::ApplicationModel::StartupTaskState state) noexcept;
};
