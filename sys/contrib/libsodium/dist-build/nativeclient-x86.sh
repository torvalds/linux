#! /bin/sh

export NACL_SDK_ROOT=${NACL_SDK_ROOT-"/opt/nacl_sdk/pepper_49"}
export NACL_TOOLCHAIN=${NACL_TOOLCHAIN-"${NACL_SDK_ROOT}/toolchain/mac_x86_glibc"}
export NACL_BIN=${NACL_BIN-"${NACL_TOOLCHAIN}/bin"}
export PREFIX="$(pwd)/libsodium-nativeclient-x86"
export PATH="${NACL_BIN}:$PATH"
export CFLAGS="-O3 -fomit-frame-pointer -fforce-addr"

mkdir -p $PREFIX || exit 1

make distclean > /dev/null

if [ -z "$LIBSODIUM_FULL_BUILD" ]; then
  export LIBSODIUM_ENABLE_MINIMAL_FLAG="--enable-minimal"
else
  export LIBSODIUM_ENABLE_MINIMAL_FLAG=""
fi


./configure ${LIBSODIUM_ENABLE_MINIMAL_FLAG} \
            --host=i686-nacl \
            --disable-ssp --without-pthreads \
            --prefix="$PREFIX" || exit 1

NPROCESSORS=$(getconf NPROCESSORS_ONLN 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null)
PROCESSORS=${NPROCESSORS:-3}

make -j${PROCESSORS} check && make -j${PROCESSORS} install || exit 1
