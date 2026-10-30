#!/usr/bin/env bash
# Builds the Haiku package (.hpkg) on Haiku. Run from the repository root.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
NAME=$(awk '$1 == "name" { print $2; exit }' resources/docker-desktop.PackageInfo)
VERSION=$(awk '$1 == "version" { print $2; exit }' resources/docker-desktop.PackageInfo)
ARCH=$(awk '$1 == "architecture" { print $2; exit }' resources/docker-desktop.PackageInfo)
FILE="$ROOT/artifacts/$NAME-$VERSION-$ARCH.hpkg"
make -j8
STAGE=$(mktemp -d /tmp/docker-desktop-package-XXXXXX)
trap 'rm -rf -- "$STAGE"' EXIT
DOCS="$STAGE/documentation/packages/dockerdesktop"
mkdir -p "$STAGE/apps" "$DOCS" "$STAGE/data/deskbar/menu/Developer" \
	"$STAGE/data/licenses" "$ROOT/artifacts"
# Installed as "Docker Desktop": the Deskbar names a running app by its file.
cp build-haiku/DockerDesktop "$STAGE/apps/Docker Desktop"
strip --strip-debug "$STAGE/apps/Docker Desktop"
# GNU strip removes the appended Haiku resources; restore them.
xres -o "$STAGE/apps/Docker Desktop" build-haiku/docker-desktop.rsrc
cp resources/docker-desktop.PackageInfo "$STAGE/.PackageInfo"
cp README.md "$DOCS/"
[ -f LICENSE ] && cp LICENSE "$DOCS/"
ln -s "../../../../apps/Docker Desktop" "$STAGE/data/deskbar/menu/Developer/Docker Desktop"
package create -C "$STAGE" "$FILE"
printf '%s\n' "$FILE"
