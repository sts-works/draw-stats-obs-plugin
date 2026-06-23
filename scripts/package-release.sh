#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="${1:-0.1.0}"
out_dir="$repo_root/dist"
zip_path="$out_dir/draw-stats-obs-plugin.zip"

rm -rf "$out_dir"
mkdir -p "$out_dir"

(
  cd "$repo_root"
  zip -q -r "$zip_path" README.md LICENSE CHANGELOG.md plugin scenes
)

echo "$zip_path"
