#!/usr/bin/env bash
# Build all libraries FreeFileSync needs patched or newer than distros ship, in dependency order.
# Each step is skipped if that version (with the current patch) is already installed.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"

"$SCRIPT_DIR/build-openssl.sh"
"$SCRIPT_DIR/build-libssh2.sh" # needs OpenSSL
if [[ "${FFS_TARGET:-}" == macos-* || ( -z "${FFS_TARGET:-}" && "$(uname -s)" = Darwin ) ]]; then
    "$SCRIPT_DIR/build-libidn2.sh" # macOS only; Linux uses the distribution's libidn2
fi
"$SCRIPT_DIR/build-curl.sh"    # needs OpenSSL and libssh2 (and libidn2 on macOS)
"$SCRIPT_DIR/build-wxwidgets.sh"
