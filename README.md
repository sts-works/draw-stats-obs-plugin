# Draw Stats OBS Plugin

Draw Stats OBS Plugin is a standalone Draw Stats capture client inside OBS Studio. It captures input metadata locally, provides onboarding and a live dashboard, reconciles aggregate results with the Draw Stats API, and renders selected dashboard elements as a transparent Browser Source.

The plugin does not require Draw Stats Capture or another desktop client to be running.

## Install

1. Install the release plugin bundle for macOS or Windows in the OBS plugin directory.
2. Start OBS and sign in from the Draw Stats dock.
3. Select a drawing app or an exact running process.
4. Set the idle threshold and start recording.
5. Choose the dashboard elements to show and press `Add overlay`.

The plugin creates or updates a `Draw Stats Overlay` Browser Source at 1280 by 720 and fits it to the current OBS canvas. The overlay is served only from the plugin's loopback server and contains sanitized aggregate state.

## Capture Model

- macOS uses a listen-only `CGEventTap` helper process.
- Windows uses a Raw Input message-only helper window.
- Raw keys, typed text, pointer coordinates, and window titles are never stored or sent.
- The dock updates immediately from local events and sends only aggregate batches to the production API.
- API responses reconcile local counters without blocking the local dashboard or stream overlay.

## Build

The project follows the OBS plugin template dependency layout. Set `DRAW_STATS_OBS_BUILD_ROOT`, configure the matching preset, and build `macos` or `windows-x64`.

```text
cmake --preset macos
cmake --build --preset macos
```

The native bundle includes the input helper, localized dock resources, Qt TLS backends, and the transparent overlay application.

## Compatibility

- OBS Studio 32.1.2 is the pinned build and QA target.
- macOS 12 or newer, Apple silicon.
- Windows 10/11 x64.
- Production sign-in opens in the system browser and returns through a loopback callback.

## License

MIT. Draw Stats brand assets remain Draw Stats trademarks and should not be presented as an unrelated product.
