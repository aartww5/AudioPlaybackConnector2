#include <pch.h>

#include <app/SingleInstanceGuard.hpp>

/*------------------------------------------------------------------------------------------------------------*/
/*//////// Public Interface //////////////////////////////////////////////////////////////////////////////////*/
/*------------------------------------------------------------------------------------------------------------*/

bool SingleInstanceGuard::TryAcquire(std::wstring_view mutexName) noexcept {
    if (mutexName.empty()) return false;

    m_mutex.reset(CreateMutexW(nullptr, TRUE, std::wstring(mutexName).c_str()));
    if (!m_mutex) return false;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        m_mutex.reset();
        return false;
    }

    return true;
}

void SingleInstanceGuard::Release() noexcept {
    m_mutex.reset();
}
