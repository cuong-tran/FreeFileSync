#!/usr/bin/env bash
# Download the pinned sentry-cli for this Linux machine's architecture, or the universal macOS one (checksums below), to $FFS_BUILD_DIR/bin/sentry-cli
# and print its path. CI uses it to check and upload the debug files of a FFS_SENTRY=1 build (see package.sh).
# Usage: fetch-sentry-cli.sh   (SENTRY_CLI_VERSION overrides the version; then the download is not verified)
set -euo pipefail
# shellcheck source=./common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

SENTRY_CLI_VERSION="${SENTRY_CLI_VERSION:-3.8.2}"
# here, not in common.sh: common.sh is part of every library cache key, and sentry-cli builds no library
KNOWN_SHA256[sentry-cli-3.8.2-Linux-x86_64]=c958865f9d4000183509ea21b1f04925d5390de6d8d87386b9e69f2b305070c3
KNOWN_SHA256[sentry-cli-3.8.2-Linux-aarch64]=b6e49fca868a36d20da2ada1832a0ce2f9ac7012c128207518b09d6b6c991f4d
KNOWN_SHA256[sentry-cli-3.8.2-Darwin-universal]=2d1b118fa46960ba5e8e0cca9a6e197366905776a0ddc5a89c7d05a3908b77db
[[ "$SENTRY_CLI_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid SENTRY_CLI_VERSION '$SENTRY_CLI_VERSION'"
if [ "$(uname -s)" = Darwin ]; then
    CLI_OS=Darwin CLI_ARCH=universal # one binary for arm64 and x86_64 Macs
else
    [ "$FFS_TARGET" = linux ] || die "fetch-sentry-cli.sh: Linux or macOS"
    CLI_OS=Linux
    case "$(uname -m)" in
        x86_64)        CLI_ARCH=x86_64 ;;
        aarch64|arm64) CLI_ARCH=aarch64 ;;
        *)             die "no sentry-cli for $(uname -m)" ;;
    esac
fi

FILE="sentry-cli-$SENTRY_CLI_VERSION-$CLI_OS-$CLI_ARCH" # the release asset is "sentry-cli-<os>-<arch>"; versioned name in archives/
download_archive "$FILE" "https://github.com/getsentry/sentry-cli/releases/download/$SENTRY_CLI_VERSION/sentry-cli-$CLI_OS-$CLI_ARCH"
mkdir -p "$BUILD_DIR/bin"
install -m 755 "$ARCHIVE_DIR/$FILE" "$BUILD_DIR/bin/sentry-cli"
echo "$BUILD_DIR/bin/sentry-cli"
