#!/usr/bin/env bash
# Build the DockerDesktop .hpkg on Linux with the host Haiku `package` tool,
# from the cross-built binary in build-cross/. (Arm64 hpkgs are built by
# air/OS CI, which has the aarch64 Haiku cross tools.)
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

PACKAGE_TOOL=${PACKAGE_TOOL:-/mnt/HaikuWork/x399/build/x86_64/objects/linux/x86_64/release/tools/package/package}
VERSION=0.1.0~dev
BUILD=${BUILD:-1}

if [ ! -f build-cross/DockerDesktop ]; then
    echo ">> cross-building first"
    bash tools/build-cross.sh
fi

STAGE=$(mktemp -d /tmp/dockerdesktop-pkg-XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
mkdir -p "$ROOT/artifacts" "$STAGE/apps" \
    "$STAGE/documentation/packages/dockerdesktop" \
    "$STAGE/data/deskbar/menu/Developer" "$STAGE/data/licenses"

cp build-cross/DockerDesktop "$STAGE/apps/DockerDesktop"
cp resources/docker-desktop.PackageInfo "$STAGE/.PackageInfo"
cp README.md "$STAGE/documentation/packages/dockerdesktop/"
[ -f LICENSE ] && cp LICENSE "$STAGE/documentation/packages/dockerdesktop/"
ln -s ../../../../apps/DockerDesktop \
    "$STAGE/data/deskbar/menu/Developer/DockerDesktop"

out="$ROOT/artifacts/dockerdesktop-${VERSION}-${BUILD}-x86_64.hpkg"
"$PACKAGE_TOOL" create -C "$STAGE" "$out"
echo ">> $out"
