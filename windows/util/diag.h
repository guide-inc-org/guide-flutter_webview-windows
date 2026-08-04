#pragma once

// TEMPORARY DIAGNOSTICS -- remove once the blank-webview investigation is
// finished.
//
// Enabled at runtime by setting the WEBVIEW_WINDOWS_DIAG environment variable,
// so a build carrying this code stays silent for normal users:
//
//   set WEBVIEW_WINDOWS_DIAG=1
//   HYPERSBIFX.exe > webview-diag.txt 2>&1
//
// The host runner calls AttachConsole(ATTACH_PARENT_PROCESS), so std::cerr
// from the plugin reaches the console it was launched from.

#ifdef _WIN32
#include <windows.h>
#endif

namespace diag {

inline bool enabled() {
  static const bool value = []() -> bool {
#ifdef _WIN32
    return GetEnvironmentVariableW(L"WEBVIEW_WINDOWS_DIAG", nullptr, 0) != 0;
#else
    return false;
#endif
  }();
  return value;
}

}  // namespace diag
