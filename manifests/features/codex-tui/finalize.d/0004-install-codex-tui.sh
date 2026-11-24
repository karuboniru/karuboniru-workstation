#!/usr/bin/env bash
set -euo pipefail

archive=$(mktemp)
trap 'rm -f -- "$archive"' EXIT
curl -fL --retry 3 -o "$archive" \
    https://github.com/openai/codex/releases/latest/download/codex-package-x86_64-unknown-linux-musl.tar.zst
tar --zstd -xf "$archive" -C usr/bin --no-same-owner --strip-components=1 --wildcards 'bin/*'
