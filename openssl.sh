#!/bin/bash
if [[ ! -d ./openssl ]];
then
  git clone --depth 1 https://github.com/openssl/openssl.git --branch OpenSSL_1_1_0-stable ./openssl
fi
cd openssl
git fetch --depth 1


export INSTALL_DIR=$(pwd)/arm
export PATH=$INSTALL_DIR/bin:$PATH
export TARGETMACH=arm-none-linux-gnueabihf
export BUILDMACH=i686-pc-linux-gnu
export CROSS=arm-linux-gnueabihf
export CC=${CROSS}-gcc
export LD=${CROSS}-ld
export AS=${CROSS}-as
export AR=${CROSS}-ar

make clean
./Configure linux-generic32 -DOPENSSL_NO_HEARTBEATS --prefix=$INSTALL_DIR --openssldir=${INSTALL_DIR}/final shared
echo $?
echo "========================================================"

make
#cd ${INSTALL_DIR}/
#cd lib
#$AR -x libcrypto.a
#$CC -shared *.o -o libcrypto.so
#rm *.o
#$AR -x libssl.a
#$CC -shared *.o -o libssl.so
#rm *.o

#checkinstall
