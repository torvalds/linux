# Travis Testing

Unbound 1.10 and above leverage Travis CI to increase coverage of compilers and platforms. Compilers include Clang and GCC; while platforms include Android, iOS, Linux, and OS X on AMD64, Aarch64, PowerPC and s390x hardware.

Android is tested on armv7a, aarch64, x86 and x86_64. The Android recipes build and install OpenSSL and Expat, and then builds Unbound. The testing is tailored for Android NDK-r19 and above, and includes NDK-r20 and NDK-r21. Mips and Mips64 are not tested because they are no longer supported under current NDKs.

iOS is tested for iPhoneOS, WatchOS, AppleTVOS, iPhoneSimulator, AppleTVSimulator and WatchSimulator. The testing uses Xcode 10 on OS X 10.13.

The Unbound Travis configuration file `.travis.yml` does not use top-level keys like `os:` and `compiler:` so there is no matrix expansion. Instead Unbound specifies the exact job to run under the `jobs:` and `include:` keys.

## Typical recipe

A typical recipe tests Clang and GCC on various hardware. The hardware includes AMD64, Aarch64, PowerPC and s390x. PowerPC is a little-endian platform, and s390x is a big-endian platform. There are pairs of recipes that are similar to the following.

```
- os: linux
  name: GCC on Linux, Aarch64
  compiler: gcc
  arch: arm64
  dist: bionic
- os: linux
  name: Clang on Linux, Aarch64
  compiler: clang
  arch: arm64
  dist: bionic
```

OS X provides a single recipe to test Clang. GCC is not tested because GCC is an alias for Clang.

## Sanitizer builds

Two sanitizer builds are tested using Clang and GCC, for a total of four builds. The first sanitizer is Undefined Behavior sanitizer (UBsan), and the second is Address sanitizer (Asan). The sanitizers are only run on AMD64 hardware. Note the environment includes `TEST_UBSAN=yes` or `TEST_ASAN=yes` for the sanitizer builds.

The recipes are similar to the following.

```
- os: linux
  name: UBsan, GCC on Linux, Amd64
  compiler: gcc
  arch: amd64
  dist: bionic
  env: TEST_UBSAN=yes
- os: linux
  name: UBsan, Clang on Linux, Amd64
  compiler: clang
  arch: amd64
  dist: bionic
  env: TEST_UBSAN=yes
```

When the Travis script encounters a sanitizer it uses different `CFLAGS` and configuration string.

```
if [ "$TEST_UBSAN" = "yes" ]; then
  export CFLAGS="-DNDEBUG -g2 -O3 -fsanitize=undefined -fno-sanitize-recover"
  ./configure
  make -j 2
  make test
elif [ "$TEST_ASAN" = "yes" ]; then
  export CFLAGS="-DNDEBUG -g2 -O3 -fsanitize=address"
  ./configure
  make -j 2
  make test
...
```

## Android builds

Travis tests Android builds for the armv7a, aarch64, x86 and x86_64 architectures. The builds are trickier than other builds for several reasons. The testing requires installation of the Android NDK and SDK, it requires a cross-compile, and requires OpenSSL and Expat prerequisites. The Android cross-compiles also require care to set the Autotools triplet, the OpenSSL triplet, the toolchain path, the tool variables, and the sysroot. The discussion below detail the steps of the Android recipes.

### Android job

The first step sets environmental variables for the cross-compile using the Travis job. A typical job with variables is shown below.

```
- os: linux
  name: Android armv7a, Linux, Amd64
  compiler: clang
  arch: amd64
  dist: bionic
  env:
    - TEST_ANDROID=yes
    - AUTOTOOLS_HOST=armv7a-linux-androideabi
    - OPENSSL_HOST=android-arm
    - ANDROID_CPU=armv7a
    - ANDROID_API=23
    - ANDROID_PREFIX="$HOME/android$ANDROID_API-$ANDROID_CPU"
    - ANDROID_SDK_ROOT="$HOME/android-sdk"
    - ANDROID_NDK_ROOT="$HOME/android-ndk"
```

### ANDROID_NDK_ROOT

The second step for Android is to set the environmental variables `ANDROID_NDK_ROOT` and `ANDROID_SDK_ROOT`. This is an important step because the NDK and SDK use the variables internally to locate their own tools. Also see [Recommended NDK Directory?](https://groups.google.com/forum/#!topic/android-ndk/qZjhOaynHXc) on the android-ndk mailing list. (Many folks miss this step, or use incorrect variables like `ANDROID_NDK_HOME` or `ANDROID_SDK_HOME`).

If you are working from a developer machine you probably already have the necessary tools installed. You should ensure `ANDROID_NDK_ROOT` and `ANDROID_SDK_ROOT` are set properly.

### Tool installation

The second step installs tools needed for OpenSSL, Expat and Unbound. This step is handled in by the script `contrib/android/install_tools.sh`. The tools include curl, tar, zip, unzip and java.

```
before_script:
  - |
    if [ "$TEST_ANDROID" = "yes" ]; then
      ./contrib/android/install_tools.sh
    elif [ "$TEST_IOS" = "yes" ]; then
      ./contrib/ios/install_tools.sh
    fi
```

### NDK installation

The third step installs the NDK and SDK. This step is handled in by the script `contrib/android/install_ndk.sh`. The script uses `ANDROID_NDK_ROOT` and `ANDROID_SDK_ROOT` to place the NDK and SDK in the `$HOME` directory.

If you are working from a developer machine you probably already have a NDK and SDK installed.

### Android environment

The fourth step sets the Android cross-compile environment using the script `contrib/android/setenv_android.sh`. The script is `sourced` so the variables in the script are available to the calling shell. The script sets variables like `CC`, `CXX`, `AS` and `AR`; sets `CFLAGS` and `CXXFLAGS`; sets a `sysroot` so Android headers and libraries are found; and adds the path to the toolchain to `PATH`.

`contrib/android/setenv_android.sh` knows which toolchain and architecture to select by inspecting environmental variables set by Travis for the job. In particular, the variables `ANDROID_CPU` and `ANDROID_API` tell `contrib/android/setenv_android.sh` which tools and libraries to select.

The `contrib/android/setenv_android.sh` script specifies the tools in a `case` statement like the following. There is a case for each of the architectures armv7a, aarch64, x86 and x86_64.

```
armv8a|aarch64|arm64|arm64-v8a)
  CC="aarch64-linux-android$ANDROID_API-clang"
  CXX="aarch64-linux-android$ANDROID_API-clang++"
  LD="aarch64-linux-android-ld"
  AS="aarch64-linux-android-as"
  AR="aarch64-linux-android-ar"
  RANLIB="aarch64-linux-android-ranlib"
  STRIP="aarch64-linux-android-strip"

  CFLAGS="-funwind-tables -fexceptions"
  CXXFLAGS="-funwind-tables -fexceptions -frtti"
```

### OpenSSL and Expat

The fifth step builds OpenSSL and Expat. OpenSSL and Expat are built for Android using the scripts `contrib/android/install_openssl.sh` and `contrib/android/install_expat.sh`. The scripts download, configure and install the latest release version of the libraries. The libraries are configured with `--prefix="$ANDROID_PREFIX"` so the headers are placed in `$ANDROID_PREFIX/include` directory, and the libraries are placed in the `$ANDROID_PREFIX/lib` directory.

`ANDROID_PREFIX` is the value `$HOME/android$ANDROID_API-$ANDROID_CPU`. The libraries will be installed in `$HOME/android23-armv7a`, `$HOME/android23-aarch64`, etc. For Autotools projects, the appropriate `PKG_CONFIG_PATH` is exported. `PKG_CONFIG_PATH` is the userland equivalent to sysroot, and allows Autotools to find non-system headers and libraries for an architecture. Typical `PKG_CONFIG_PATH` are `$HOME/android23-armv7a/lib/pkgconfig` and `$HOME/android23-aarch64/lib/pkgconfig`.

OpenSSL also uses a custom configuration file called `15-android.conf`. It is a copy of the OpenSSL's project file and located at `contrib/android/15-android.conf`. The Unbound version is copied to the OpenSSL source files after unpacking the OpenSSL distribution. The Unbound version has legacy NDK support removed and some other fixes, like `ANDROID_NDK_ROOT` awareness. The changes mean Unbound's `15-android.conf` will only work with Unbound, with NDK-r19 and above, and a properly set environment.

OpenSSL is configured with `no-engine`. If you want to include OpenSSL engines then edit `contrib/android/install_openssl.sh` and remove the config option.

### Android build

Finally, once OpenSSL and Expat are built, then the Travis script configures and builds Unbound. The recipe looks as follows.

```
elif [ "$TEST_ANDROID" = "yes" ]; then
  export AUTOTOOLS_BUILD="$(./config.guess)"
  export PKG_CONFIG_PATH="$ANDROID_PREFIX/lib/pkgconfig"
  ./contrib/android/install_ndk.sh
  source ./contrib/android/setenv_android.sh
  ./contrib/android/install_openssl.sh
  ./contrib/android/install_expat.sh
  ./configure \
    --build="$AUTOTOOLS_BUILD" \
    --host="$AUTOTOOLS_HOST" \
    --prefix="$ANDROID_PREFIX" \
    --with-ssl="$ANDROID_PREFIX" \
    --with-libexpat="$ANDROID_PREFIX" \
    --disable-gost;
  make -j 2
  make install
```

Travis only smoke tests an Android build using a compile, link and install. The self tests are not run. TODO: figure out how to fire up an emulator, push the tests to the device and run them.

### Android flags

`contrib/android/setenv_android.sh` uses specific flags for `CFLAGS` and `CXXFLAGS`. They are taken from `ndk-build`, so we consider them the official flag set. It is important to use the same flags across projects to avoid subtle problems due to mixing and matching different flags.

`CXXFLAGS` includes `-fexceptions` and `-frtti` because exceptions and runtime type info are disabled by default. `CFLAGS` include `-funwind-tables` and `-fexceptions` to ensure C++ exceptions pass through C code, if needed. Also see `docs/CPLUSPLUS-SUPPORT.html` in the NDK docs.

To inspect the flags used by `ndk-build` for a platform clone ASOP's [ndk-samples](https://github.com/android/ndk-samples/tree/master/hello-jni) and build the `hello-jni` project. Use the `V=1` flag to see the full compiler output from `ndk-build`.

## iOS builds

Travis tests iOS builds for the armv7a, armv7s and aarch64 architectures for iPhoneOS, AppleTVOS and WatchOS. iPhoneOS is tested using both 32-bit builds (iPhones) and 64-bit builds (iPads). Travis also tests compiles against the simulators. The builds are trickier than other builds for several reasons. The testing requires a cross-compile, and requires OpenSSL and Expat prerequisites. The iOS cross-compiles also require care to set the Autotools triplet, the OpenSSL triplet, the toolchain path, the tool variables, and the sysroot. The discussion below detail the steps of the iOS recipes.

### iOS job

The first step sets environmental variables for the cross-compile using the Travis job. A typical job with variables is shown below.

```
- os: osx
  osx_image: xcode10
  name: Apple iPhone on iOS, armv7
  compiler: clang
  env:
    - TEST_IOS=yes
    - AUTOTOOLS_HOST=armv7-apple-ios
    - OPENSSL_HOST=ios-cross
    - IOS_SDK=iPhoneOS
    - IOS_CPU=armv7s
    - IOS_PREFIX="$HOME/$IOS_SDK-$IOS_CPU"
```

### Tool installation

The second step installs tools needed for OpenSSL, Expat and Unbound. This step is handled in by the script `contrib/ios/install_tools.sh`. The tools include autotools, curl and perl. The installation happens at the `before_script:` stage of Travis.

```
before_script:
  - |
    if [ "$TEST_ANDROID" = "yes" ]; then
      ./contrib/android/install_tools.sh
    elif [ "$TEST_IOS" = "yes" ]; then
      ./contrib/ios/install_tools.sh
    fi
```

### iOS environment

The third step sets the iOS cross-compile environment using the script `contrib/ios/setenv_ios.sh`. The script is `sourced` so the variables in the script are available to the calling shell. The script sets variables like `CC`, `CXX`, `AS` and `AR`; sets `CFLAGS` and `CXXFLAGS`; sets a `sysroot` so iOS headers and libraries are found; and adds the path to the toolchain to `PATH`.

`contrib/ios/setenv_ios.sh` knows which toolchain and architecture to select by inspecting environmental variables set by Travis for the job. In particular, the variables `IOS_SDK` and `IOS_CPU` tell `contrib/ios/setenv_ios.sh` which tools and libraries to select.

The `contrib/ios/setenv_ios.sh` script specifies the tools to use during the cross-compile. For Apple SDKs, the tool names are the same as a desktop. There are no special prefixes for the mobile tools.

```
CPP=cpp
CC=clang
CXX=clang++
LD=ld
AS=as
AR=ar
RANLIB=ranlib
STRIP=strip
```

If you are working from a developer machine you probably already have the necessary tools installed.

### OpenSSL and Expat

The fourth step builds OpenSSL and Expat. OpenSSL and Expat are built for iOS using the scripts `contrib/ios/install_openssl.sh` and `contrib/ios/install_expat.sh`. The scripts download, configure and install the latest release version of the libraries. The libraries are configured with `--prefix="$IOS_PREFIX"` so the headers are placed in `$IOS_PREFIX/include` directory, and the libraries are placed in the `$IOS_PREFIX/lib` directory.

`IOS_PREFIX` is the value `$HOME/$IOS_SDK-$IOS_CPU`. The scheme handles both iOS SDKs and cpu architectures so the pair receives a unique installation directory. The libraries will be installed in `$HOME/iPhoneOS-armv7s`, `$HOME/iPhoneOS-arm64`, `$HOME/iPhoneSimulator-i386`, etc. For Autotools projects, the appropriate `PKG_CONFIG_PATH` is exported.

`PKG_CONFIG_PATH` is an important variable. It is the userland equivalent to sysroot, and allows Autotools to find non-system headers and libraries for an architecture. Typical `PKG_CONFIG_PATH` are `$HOME/iPhoneOS-armv7s/lib/pkgconfig` and `$HOME/iPhoneOS-arm64/lib/pkgconfig`.

OpenSSL also uses a custom configuration file called `15-ios.conf`. It is a copy of the OpenSSL's project file and located at `contrib/ios/15-ios.conf`. The Unbound version is copied to the OpenSSL source files after unpacking the OpenSSL distribution. The changes mean Unbound's `15-ios.conf` will only work with Unbound and a properly set environment.

OpenSSL is configured with `no-engine`. Engines require dynamic loading so engines are disabled permanently in `15-ios.conf`.

### iOS build

Finally, once OpenSSL and Expat are built, then the Travis script configures and builds Unbound. The full recipe looks as follows.

```
elif [ "$TEST_IOS" = "yes" ]; then
  export AUTOTOOLS_BUILD="$(./config.guess)"
  export PKG_CONFIG_PATH="$IOS_PREFIX/lib/pkgconfig"
  source ./contrib/ios/setenv_ios.sh
  ./contrib/ios/install_openssl.sh
  ./contrib/ios/install_expat.sh
  ./configure \
    --build="$AUTOTOOLS_BUILD" \
    --host="$AUTOTOOLS_HOST" \
    --prefix="$IOS_PREFIX" \
    --with-ssl="$IOS_PREFIX" \
    --with-libexpat="$IOS_PREFIX" \
    --disable-gost;
  make -j 2
  make install
```

Travis only smoke tests an iOS build using a compile, link and install. The self tests are not run. TODO: figure out how to fire up an simulator, push the tests to the device and run them.

### iOS flags

`contrib/ios/setenv_ios.sh` uses specific flags for `CFLAGS` and `CXXFLAGS`. They are taken from Xcode, so we consider them the official flag set. It is important to use the same flags across projects to avoid subtle problems due to mixing and matching different flags.
