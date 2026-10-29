#!/usr/bin/env bash
# Cross-compile Docker Desktop on Linux with the x86_64 Haiku cross tools
# and the libraries of the workstation's Haiku build. Produces
# build-cross/DockerDesktop (with resources).
# Usage: tools/build-cross.sh [make-target]
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
HAIKU_SRC=${HAIKU_SRC:-/mnt/HaikuWork/src/haiku}
HAIKU_BUILD=${HAIKU_BUILD:-/mnt/HaikuWork/x399/build/x86_64}
CROSS="$HAIKU_BUILD/cross-tools-x86_64/bin/x86_64-unknown-haiku"
OBJECTS="$HAIKU_BUILD/objects/haiku/x86_64/release"
SYSLIBS=$(ls -d "$HAIKU_BUILD"/build_packages/gcc_syslibs_devel-*-x86_64 | head -1)
RUNTIME=$(ls -d "$HAIKU_BUILD"/build_packages/gcc_syslibs-*-x86_64 | head -1)
HOST_TOOLS=${HOST_TOOLS:-"$HAIKU_BUILD/objects/linux/x86_64/release/tools"}

# The C++ library headers come first: they reach the C headers with
# #include_next, which only searches the directories listed after them.
INCLUDES="-isystem $SYSLIBS/develop/headers/c++ -isystem $SYSLIBS/develop/headers/c++/x86_64-unknown-haiku"
INCLUDES+=" -isystem $SYSLIBS/develop/headers/gcc/include"
INCLUDES+=" -isystem $HAIKU_SRC/headers -isystem $HAIKU_SRC/headers/posix"
for dir in "$HAIKU_SRC"/headers/os "$HAIKU_SRC"/headers/os/*/; do
	[[ -d $dir ]] && INCLUDES+=" -isystem ${dir%/}"
done
INCLUDES+=" -isystem $HAIKU_SRC/headers/os/add-ons/graphics"
INCLUDES+=" -isystem $HAIKU_SRC/headers/glibc"

GLUE="$OBJECTS/system/glue"
CRT="$HAIKU_BUILD/cross-tools-x86_64/lib/gcc/x86_64-unknown-haiku/13.3.0"
LIBDIRS="-L$OBJECTS/kits -L$OBJECTS/kits/interface -L$OBJECTS/kits/tracker -L$OBJECTS/kits/shared -L$OBJECTS/system/libroot -L$OBJECTS/system/libnetwork -L$RUNTIME/lib"
START="-nodefaultlibs -nostartfiles $GLUE/arch/x86_64/crti.o $CRT/crtbeginS.o $GLUE/start_dyn.o $GLUE/init_term_dyn.o"
END="$CRT/crtendS.o $GLUE/arch/x86_64/crtn.o"
CXXLIBS="$RUNTIME/lib/libstdc++.so.6 $RUNTIME/lib/libgcc_s.so.1 $CRT/libgcc.a"

export CROSS_CPPFLAGS="$INCLUDES"
export PRIVATE_HEADERS="$HAIKU_SRC/headers/private"
export CXX="$CROSS-g++"
export RC="$HOST_TOOLS/rc/rc"
export XRES="$HOST_TOOLS/xres"
export MIMESET="$HOST_TOOLS/mimeset --mimedb $OBJECTS/kits/libbe.so_mimedb"
export BUILD=build-cross
export APP_LDFLAGS="$START $LIBDIRS"
export APP_LDEND="$CXXLIBS -lroot -lnetwork -lbe -lpthread $END"

cd "$ROOT"
make "$@"
