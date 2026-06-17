#include <pch.h>
#include <ui/FlyoutPresenterStyle.hpp>
#include <util/Util.hpp>

namespace apc::ui {

void StripFlyoutPresenterStyle(winrt::Microsoft::UI::Xaml::DependencyObject const& content) noexcept {
    try {
        auto parent = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(content);
        while (parent) {
            auto presenter = parent.try_as<winrt::Microsoft::UI::Xaml::Controls::FlyoutPresenter>();
            if (presenter) {
                presenter.Background(nullptr);
                presenter.BorderBrush(nullptr);
                presenter.BorderThickness({0});
                presenter.Padding({0});
                presenter.MinWidth(0);
                presenter.MinHeight(0);
                break;
            }
            parent = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetParent(parent);
        }
    } catch (winrt::hresult_error const& ex) {
        util::DebugTraceException(L"[FlyoutPresenterStyle] ERROR: StripFlyoutPresenterStyle failed", ex);
    } catch (std::exception const& ex) {
        util::DebugTraceException(L"[FlyoutPresenterStyle] ERROR: StripFlyoutPresenterStyle failed", ex);
    } catch (...) {
        util::DebugTraceUnknownException(L"[FlyoutPresenterStyle] ERROR: StripFlyoutPresenterStyle failed");
    }
}

} // namespace apc::ui
