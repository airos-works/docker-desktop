#!/usr/bin/env bash
# Build the DockerDesktop .hpkg(s) on Linux with the host Haiku `package`
# tool, from the cross-built binaries.
# Usage: tools/package-haiku-cross.sh [arch ...]   (default: x86_64)
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

PACKAGE_TOOL=${PACKAGE_TOOL:-/mnt/HaikuWork/x399/build/x86_64/objects/linux/x86_64/release/tools/package/package}
VERSION=0.1.0~dev
BUILD=${BUILD:-1}

for arch in "${@:-x86_64}"; do
    case "$arch" in
        x86_64) dir=build-cross ;;
        arm64)  dir=build-cross-arm64 ;;
        *) echo "unknown arch: $arch" >&2; exit 1 ;;
    esac
    if [ ! -f "$dir/DockerDesktop" ]; then
        echo ">> cross-building $arch first"
        bash "tools/build-cross${dir#build-cross}.sh"
    fi

    STAGE=$(mktemp -d /tmp/dockerdesktop-pkg-XXXXXX)
    mkdir -p "$ROOT/artifacts" "$STAGE/apps" \
        "$STAGE/documentation/packages/dockerdesktop" \
        "$STAGE/data/deskbar/menu/Developer" "$STAGE/data/licenses"

    cp "$dir/DockerDesktop" "$STAGE/apps/DockerDesktop"
    # The icon, signature and version as attributes too (Tracker and the
    # Deskbar read those), like CI's add_attributes.
    "$(dirname "$PACKAGE_TOOL")/../resattr/resattr" -O \
        -o "$STAGE/apps/DockerDesktop" "$dir/docker-desktop.rsrc"
    sed -e "s/^architecture x86_64/architecture $arch/" \
        -e "s/^version .*/version $VERSION-$BUILD/" \
        resources/docker-desktop.PackageInfo > "$STAGE/.PackageInfo"
    cp README.md "$STAGE/documentation/packages/dockerdesktop/"
    [ -f LICENSE ] && cp LICENSE "$STAGE/documentation/packages/dockerdesktop/"
    ln -s ../../../../apps/DockerDesktop \
        "$STAGE/data/deskbar/menu/Developer/DockerDesktop"

    out="$ROOT/artifacts/dockerdesktop-${VERSION}-${BUILD}-${arch}.hpkg"
    "$PACKAGE_TOOL" create -C "$STAGE" "$out"
    rm -rf "$STAGE"
    echo ">> $out"
done
