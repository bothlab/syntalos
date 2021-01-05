#!/bin/sh
set -e

echo "C compiler: $CC"
echo "C++ compiler: $CXX"
set -x

#
# This script is supposed to run inside the Syntalos Docker container
# on the CI system.
#

$CC --version

# configure Syntalos build with all flags enabled
mkdir build && cd build
cmake -G Ninja \
      -DMAINTAINER=ON \
      -DMOD_CAMERA_UEYE=OFF \
      ..

# Build, Test & Install
ninja
DESTDIR=/tmp/install_root/ ninja install
export CTEST_PROGRESS_OUTPUT=1
export CTEST_OUTPUT_ON_FAILURE=1
xvfb-run -a -s "-screen 0 1024x768x24 +extension GLX" \
	ninja test -v