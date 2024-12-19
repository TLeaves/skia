
$BASE_DIR = $Pwd
$BUILD_DIR = "out/pathkit_native_win"

New-Item -Type Directory -Force $BUILD_DIR

rm -Force $BUILD_DIR/*.lib

$CLANG_WIN = [Environment]::GetEnvironmentVariable('LLVM_HOME', 'Machine')
$CLANG_WIN_VERSION = 19
if ($CLANG_WIN -eq $null) {
    $CLANG_BIN_PATH = (Get-Command clang).Path
    if ($CLANG_BIN_PATH -ne $null) {
        $CLANG_BIN_DIR = [System.IO.Path]::GetDirectoryName($CLANG_BIN_PATH)
        $CLANG_WIN = [System.IO.Path]::GetDirectoryName($CLANG_BIN_DIR).Replace("\", "/")
    }
    if ($CLANG_WIN -eq $null) {
        echo "Be sure to set the LLVM_HOME environment variable or add clang location to PATH."
        exit 1
    }
}

Push-Location $BASE_DIR/../..

echo "Putting output in $BUILD_DIR (pwd = $Pwd)"

# Run this from $SKIA_HOME, not from the directory this file is in.
if (!(Test-Path -Path "./src")) {
    echo "Cannot locate Skia source. Is the source checkout okay? Exiting."
    exit 1
}

if ($Args.Count -gt 0) {
    $BUILD_ARG0 = $args[0]
}

# Use -O0 for larger builds (but generally quicker)
# Use -Oz for (much slower, but smaller/faster) production builds
$RELEASE_CONF="-O2 -DSK_RELEASE"
# It is very important for the -DSK_RELEASE/-DSK_DEBUG to match on the libskia.a, otherwise
# things like SKDEBUGCODE are sometimes compiled in and sometimes not, which can cause headaches
# like sizeof() mismatching between .cpp files and .h files.
$EXTRA_CFLAGS="\`"-DSK_RELEASE\`""
if ($BUILD_ARG0 -contains "test") {
    echo "Building a Testing/Profiling build"
    $RELEASE_CONF="-O2 -DPATHKIT_TESTING -DSK_RELEASE"
} elseif ($BUILD_ARG0 -contains "debug") {
    echo "Building a Debug build"
    $EXTRA_CFLAGS="\`"-DSK_DEBUG\`""
    $RELEASE_CONF="-O0 -g3 -DPATHKIT_TESTING -DSK_DEBUG"
}

$OUTPUT="-shared -o $BUILD_DIR/libpathkit.dll"

$CXX = "clang++"

# ./bin/fetch-ninja
$NINJA = "third_party/ninja/ninja.exe"

echo "Compiling bitcode"

if (!(Test-Path -Path "./bin/gn.exe")) {
    ./bin/fetch-gn
}

./bin/gn.exe gen $BUILD_DIR `
  --args="`"`
  clang_win=\`"$CLANG_WIN\`" `
  clang_win_version=$CLANG_WIN_VERSION `
  cc=\`"clang\`" `
  cxx=\`"clang++\`" `
  extra_cflags=[
    \`"/MT\`",
    $EXTRA_CFLAGS
  ] `
  is_debug=false `
  is_official_build=true `
  is_trivial_abi=true `
  is_component_build=false `
  werror=true `
  target_cpu=\`"x64\`" "`"

. $NINJA -C $BUILD_DIR pathkit.lib
. $NINJA -C $BUILD_DIR pathkit_native_extras.lib

echo "Generating Lib"

. $CXX $RELEASE_CONF.Split() -std=c++17 `
-I. `
-fvisibility=hidden -fvisibility-inlines-hidden `
-fno-rtti -fno-exceptions `
"-DSK_TRIVIAL_ABI=[[clang::trivial_abi]]" `
-DPATHKIT_DLL `
-DSKIA_IMPLEMENTATION `
$OUTPUT.Split() `
$BASE_DIR/src/InkStrokeUtils.cpp `
$BASE_DIR/src/PathStrokeUtils.cpp `
$BASE_DIR/pathkit_c_bindings.cpp `
$BUILD_DIR/pathkit.lib `
$BUILD_DIR/pathkit_native_extras.lib

Pop-Location