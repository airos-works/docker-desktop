#!/usr/bin/env bash
# Cross-compile Docker Desktop for air/OS arm64 (aarch64) on Linux, using
# the arm64 Haiku cross tools and the staged packages of the workstation's
# arm64 Haiku build. Produces build-cross-arm64/DockerDesktop.
# Usage: tools/build-cross-arm64.sh [make-target]
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ARM=${HAIKU_ARM64_BUILD:-/mnt/HaikuWork/build/arm64}
CROSS="$ARM/cross-tools-arm64/bin/aarch64-unknown-haiku"
STAGED="$ARM/objects/haiku/arm64/packaging/packages_build/regular"
HAIKU_DEVEL="$STAGED/hpkg_-haiku_devel.hpkg/contents"
HAIKU_RT="$STAGED/hpkg_-haiku.hpkg/contents"
GLUE="$ARM/objects/haiku/arm64/release/system/glue"
MIMEDB="$ARM/objects/haiku/arm64/release/kits/libbe.so_mimedb"
HOST_TOOLS=${HOST_TOOLS:-/mnt/HaikuWork/x399/build/x86_64/objects/linux/x86_64/release/tools}
SYSLIBS=$(ls -d "$ARM"/build_packages/gcc_syslibs_devel-*-arm64 | head -1)
RUNTIME=$(ls -d "$ARM"/build_packages/gcc_syslibs-*-arm64 | head -1)
CRT="$ARM/cross-tools-arm64/lib/gcc/aarch64-unknown-haiku/13.3.0"

# The C++ library headers come first: they reach the C headers with
# #include_next, which only searches the directories listed after them.
INCLUDES="-isystem $SYSLIBS/develop/headers/c++ -isystem $SYSLIBS/develop/headers/c++/aarch64-unknown-haiku"
INCLUDES+=" -isystem $SYSLIBS/develop/headers/gcc/include"
INCLUDES+=" -isystem $HAIKU_DEVEL/develop/headers -isystem $HAIKU_DEVEL/develop/headers/posix"
INCLUDES+=" -isystem $HAIKU_DEVEL/develop/headers/os"
for dir in "$HAIKU_DEVEL"/develop/headers/os/*/; do
	[[ -d $dir ]] && INCLUDES+=" -isystem ${dir%/}"
done
INCLUDES+=" -isystem $HAIKU_DEVEL/develop/headers/glibc"

LIBDIRS="-L$HAIKU_RT/lib -L$RUNTIME/lib -L$HAIKU_DEVEL/develop/lib"
START="-nodefaultlibs -nostartfiles $GLUE/arch/arm64/crti.o $CRT/crtbeginS.o $GLUE/start_dyn.o $GLUE/init_term_dyn.o"
END="$CRT/crtendS.o $GLUE/arch/arm64/crtn.o"
CXXLIBS="$RUNTIME/lib/libstdc++.so.6 $RUNTIME/lib/libgcc_s.so.1 $CRT/libgcc.a"

export CROSS_CPPFLAGS="$INCLUDES"
export CXX="$CROSS-g++"
export RC="$HOST_TOOLS/rc/rc"
export XRES="$HOST_TOOLS/xres"
export MIMESET="$HOST_TOOLS/mimeset --mimedb $MIMEDB"
export BUILD=build-cross-arm64
export APP_LDFLAGS="$START $LIBDIRS"
export APP_LDEND="$CXXLIBS -lroot -lnetwork -lbe -lpthread $END"

cd "$ROOT"
make "$@"
