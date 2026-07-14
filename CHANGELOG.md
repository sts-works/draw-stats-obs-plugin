# Changelog

## 0.3.0

- Converts the OBS integration into an independent native Draw Stats client.
- Adds production sign-in, app/process onboarding, idle settings, recording controls, and a local-first realtime dashboard.
- Adds privacy-limited macOS and Windows input helper processes.
- Adds a transparent localhost stream overlay with selectable recording state, metrics, 30-minute stacked timeline, and icon-backed recent activity.
- Adds API reconciliation, secure session storage, loopback-only serving, and native contract tests.
- Keeps a running-process choice stable across a background scan refresh, so the process shown in Browse apps is the process that is saved.
- Verifies helper event filtering and local metric, timeline, Recent activity, and idle projection without synthetic input.

## 0.2.0

- Adds a realtime 30-minute stacked activity timeline derived from public counter changes.
- Adds icon-backed Recent Activity for drawing, edit, tool, transform, navigation, and save events.
- Expands the recommended OBS Browser Source size to 720 by 420.
- Keeps the local visualization during API interruptions and reconciles with the next successful public API response.

## 0.1.0

- Initial public OBS Browser Source kit.
- Adds a release ZIP structure for GitHub Releases.
- Adds an OBS scene collection template for the Draw Stats panel.
