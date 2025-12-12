#!/bin/bash

set -ex

BASE_DIR=`cd $(dirname ${BASH_SOURCE[0]}) && pwd`
HTML_SHELL=$BASE_DIR/shell.html
BUILD_DIR=${BUILD_DIR:="out/pathkit_native_linux_loongarch64"}
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

# Setup cross compile environment
# http://ftp.loongnix.cn/toolchain/llvm/llvm18/llvm-toolchain_18.1.6-1_amd64-linux-gnu_debian-10.tar.gz
CLANG_HOME="/opt/loong64-cross/llvm-toolchain_18.1.6-1_amd64-linux-gnu_debian-10"
# http://ftp.loongnix.cn/toolchain/gcc/release/loongarch/gcc8/loongson-gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc1.6.tar.xz
GNU_TOOLCHAIN="/opt/loong64-cross/gnu-toolchain-8.3-x86_64-loongarch64-linux-gnu-rc"
# http://ftp.loongnix.cn/browser/build/sysroot/debian_bullseye_loong64-sysroot.tar.bz2
SYSROOT="/opt/loong64-cross/sysroot"
CROSS_COMPILER_NAME="loongarch64-linux-gnu"
CROSS_COMPILE_TARGET="--target=${CROSS_COMPILER_NAME}"

export LD_LIBRARY_PATH="${GNU_TOOLCHAIN}/x86_64-pc-linux-gnu/${CROSS_COMPILER_NAME}/lib:${LD_LIBRARY_PATH}"

# CXX=`which clang++`
CXX="${CLANG_HOME}/bin/clang++"

# ./bin/fetch-ninja
NINJA=third_party/ninja/ninja

echo "Compiling bitcode"

if [[ ! -f ./bin/gn ]]; then
  ./bin/fetch-gn
fi

./bin/gn gen ${BUILD_DIR} \
  --args="\
  extra_cflags=[
    ${EXTRA_CFLAGS},
    \"${CROSS_COMPILE_TARGET}\",
    \"--sysroot=${SYSROOT}\",
    \"--gcc-toolchain=${GNU_TOOLCHAIN}\",
    \"-I${SYSROOT}/usr/include/c++/8\",
    \"-I${SYSROOT}/usr/include/${CROSS_COMPILER_NAME}/c++/8\",
    \"-I${SYSROOT}/usr/include/${CROSS_COMPILER_NAME}\"
  ] \
  extra_ldflags=[
    \"${CROSS_COMPILE_TARGET}\",
    \"--sysroot=${SYSROOT}\",
  ] \
  extra_asmflags=[
    \"${CROSS_COMPILE_TARGET}\"
  ] \
  cc=\"${CLANG_HOME}/bin/clang\" \
  cxx=\"${CLANG_HOME}/bin/clang++\" \
  ar=\"${CLANG_HOME}/bin/llvm-ar\" \
  is_debug=false \
  is_official_build=true \
  is_trivial_abi=true \
  is_component_build=false \
  werror=true \
  target_cpu=\"loong64\" "

${NINJA} -C ${BUILD_DIR} libpathkit.a
${NINJA} -C ${BUILD_DIR} libpathkit_native_extras.a

echo "Generating Lib"

${CXX} $RELEASE_CONF -std=c++17 \
${CROSS_COMPILE_TARGET} \
--sysroot=${SYSROOT} \
--gcc-toolchain=${GNU_TOOLCHAIN} \
-I${SYSROOT}/usr/include/c++/8 \
-I${SYSROOT}/usr/include/${CROSS_COMPILER_NAME}/c++/8 \
-I${SYSROOT}/usr/include/${CROSS_COMPILER_NAME} \
-L${SYSROOT}/usr/lib/gcc/${CROSS_COMPILER_NAME}/8/ \
-B${SYSROOT}/usr/lib/gcc/${CROSS_COMPILER_NAME}/8/ \
-fuse-ld=lld \
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
