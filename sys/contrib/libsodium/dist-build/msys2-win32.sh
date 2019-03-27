#! /bin/sh

export CFLAGS="-Ofast -fomit-frame-pointer -m32 -march=pentium3 -mtune=westmere"
export PREFIX="$(pwd)/libsodium-win32"

if (i686-w64-mingw32-gcc --version > /dev/null 2>&1) then
  echo MinGW found
else
  echo Please install mingw-w64-i686-gcc >&2
  exit
fi

./configure --prefix="$PREFIX" --exec-prefix="$PREFIX" \
            --host=i686-w64-mingw32 && \
make clean && \
make && \
make check && \
make install
