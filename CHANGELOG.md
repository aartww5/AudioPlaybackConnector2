# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## pre-release [0.1.0] - 2025-05-08

### Added
- Initial release of AudioPlaybackConnector2.
- System tray-only WinUI 3 Desktop application packaged as MSIX.
- Bluetooth A2DP audio sink connection via Win32 `AudioPlaybackConnection` API.
- Left-click device picker with connect / disconnect actions.
- Right-click tray context menu (Settings, Bluetooth Settings, Exit).
- Auto-reconnect on startup with per-device granularity.
- Animated tray icon during connection and theme-aware icons for light / dark taskbars.
- JSON-based settings persistence.
- Multi-language support for 8 locales: English, German, French, Spanish, Japanese, Korean, Chinese Simplified, Chinese Traditional.
- CI/CD workflows: build (with clang-format and CppCheck), CodeQL analysis, and automated MSIX releases on version tags.

[Unreleased]: https://github.com/N0ahTM/AudioPlaybackConnector2/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/N0ahTM/AudioPlaybackConnector2/releases/tag/v0.1.0
