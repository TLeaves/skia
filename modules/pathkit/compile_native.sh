#!/bin/bash

set -ex

BASE_DIR=`cd $(dirname ${BASH_SOURCE[0]}) && pwd`
HTML_SHELL=$BASE_DIR/shell.html
BUILD_DIR=${BUILD_DIR:="out/pathkit_native_linux"}
mkdir -p $BUILD_DIR
# sometimes the .a files keep old symbols around - cleaning them out makes sure
# we get a fresh build.
rm -f $BUILD_DIR/*.a

# Navigate to SKIA_HOME from where this file is located.
pushd $BASE_DIR/../..

echo "Putting output in $BUILD_DIR (pwd = `pwd`)"

# Run this from $SKIA_HOME, not from the directory this file is in.
if [[ ! -d ./src ]]; then
  echo "Cannot locate Skia source. Is the source checkout okay? Exiting."
  exit 1
fi

if [[ $@ == *help* ]]; then
  echo "By default, this script builds a production WASM build of PathKit."
  echo ""
  echo "It is put in ${BUILD_DIR}, configured by the BUILD_DIR environment"
  echo "variable. Additionally, the EMSDK environment variable must be set."
  echo "This script takes several optional parameters:"
  echo "  test = Make a build suitable for running tests or profiling"
  echo "  debug = Make a build suitable for debugging (defines SK_DEBUG)"
  echo "  asm.js = Build for asm.js instead of WASM (very experimental)"
  echo "  serve = starts a webserver allowing a user to navigate to"
  echo "          localhost:8000/pathkit.html to view the demo page."
  exit 0
fi


# Use -O0 for larger builds (but generally quicker)
# Use -Oz for (much slower, but smaller/faster) production builds
RELEASE_CONF="-O2 -DSK_RELEASE"
# It is very important for the -DSK_RELEASE/-DSK_DEBUG to match on the libskia.a, otherwise
# things like SKDEBUGCODE are sometimes compiled in and sometimes not, which can cause headaches
# like sizeof() mismatching between .cpp files and .h files.
EXTRA_CFLAGS="\"-DSK_RELEASE\""
if [[ $@ == *test* ]]; then
  echo "Building a Testing/Profiling build"
  RELEASE_CONF="-O2 -DPATHKIT_TESTING -DSK_RELEASE"
elif [[ $@ == *debug* ]]; then
  echo "Building a Debug build"
  EXTRA_CFLAGS="\"-DSK_DEBUG\""
  RELEASE_CONF="-O0 -g3 -DPATHKIT_TESTING -DSK_DEBUG"
fi

OUTPUT="-shared -o $BUILD_DIR/libpathkit.so"

CXX=`which clang++`

# ./bin/fetch-ninja
NINJA=third_party/ninja/ninja

echo "Compiling bitcode"

if [[ ! -f ./bin/gn ]]; then
  ./bin/fetch-gn
fi

./bin/gn gen ${BUILD_DIR} \
  --args="\
  extra_cflags=[
    # \"-sMAIN_MODULE=1\",
    ${EXTRA_CFLAGS}
  ] \
  is_debug=false \
  is_official_build=true \
  is_trivial_abi=true \
  is_component_build=false \
  werror=true \
  target_cpu=\"x64\" "

${NINJA} -C ${BUILD_DIR} libpathkit.a
${NINJA} -C ${BUILD_DIR} libpathkit_native_extras.a

echo "Generating Lib"

${CXX} $RELEASE_CONF -std=c++17 \
-I. \
-fPIC \
-fvisibility=hidden -fvisibility-inlines-hidden \
-fno-rtti -fno-exceptions \
"-DSK_TRIVIAL_ABI=[[clang::trivial_abi]]" \
-DPATHKIT_DLL \
$OUTPUT \
$BASE_DIR/src/InkStrokeUtils.cpp \
$BASE_DIR/src/PathStrokeUtils.cpp \
$BASE_DIR/pathkit_c_bindings.cpp \
${BUILD_DIR}/libpathkit.a \
${BUILD_DIR}/libpathkit_native_extras.a
