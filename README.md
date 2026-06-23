# Draw Stats OBS Plugin

Draw Stats OBS Plugin is an OBS Browser Source kit for showing a public Draw Stats profile panel in streams and recordings.

This package is distributed through GitHub Releases. It does not install a native OBS binary; it uses OBS Studio's built-in Browser Source, which is the stable path for web overlays.

## Install

1. Download the latest release ZIP.
2. In OBS Studio, add `Sources` -> `Browser`.
3. Use this URL:

```text
https://drawstats.sts.works/downloads/releases/obs-browser-source/obs_browser_source_panel.html?user_id=YOUR_USER_ID
```

4. Set width to `420` and height to `260`.
5. Enable transparent background when your OBS build exposes that option.

Replace `YOUR_USER_ID` with the Draw Stats public user id for the profile you want to show.

## Release Contents

- `plugin/obs_browser_source_panel.html`: pointer to the hosted OBS Browser Source.
- `scenes/draw-stats-obs-scene.json`: OBS scene collection template.
- `README.md`: install and release notes.

## Compatibility

- OBS Studio 30 or newer is recommended.
- The Draw Stats profile must be public.
- The Browser Source refreshes every 30 seconds.

## License

MIT. Draw Stats brand assets remain Draw Stats trademarks and should not be presented as an unrelated product.
