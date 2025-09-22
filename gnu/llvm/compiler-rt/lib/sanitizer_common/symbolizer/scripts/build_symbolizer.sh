#!/usr/bin/env bash
#
# Run as: CLANG=bin/clang build_symbolizer.sh out.o
# If you want to use a local copy of zlib, set ZLIB_SRC.
# zlib can be downloaded from http://www.zlib.net.
#
# Script compiles self-contained object file with symbolization code.
#
# Symbols exported by the object file will be used by Sanitizer runtime
# libraries to symbolize code/data in-process.
#
# FIXME: We should really be using a simpler approach to building this object
# file, and it should be available as a regular cmake rule. Conceptually, we
# want to be doing "ld -r" followed by "objcopy -G" to create a relocatable
# object file with only our entry points exposed. However, this does not work at
# present, see https://github.com/llvm/llvm-project/issues/30098.

set -x
set -e
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
SRC_DIR=$(readlink -f $SCRIPT_DIR/..)

if [[ $# -ne 1 ]]; then
  echo "Missing output file"
  exit 1
fi

OUTPUT=$(readlink -f $1)
COMPILER_RT_SRC=$(readlink -f ${SCRIPT_DIR}/../../../..)
LLVM_SRC=${LLVM_SRC:-${COMPILER_RT_SRC}/../llvm}
LLVM_SRC=$(readlink -f $LLVM_SRC)

CLANG="${CLANG:-`which clang`}"
CLANG_DIR=$(readlink -f $(dirname "$CLANG"))

CC=$CLANG_DIR/clang
CXX=$CLANG_DIR/clang++
TBLGEN=$CLANG_DIR/llvm-tblgen
OPT=$CLANG_DIR/opt
AR=$CLANG_DIR/llvm-ar
LINK=$CLANG_DIR/llvm-link

for F in $CC $CXX $TBLGEN $LINK $OPT $AR; do
  if [[ ! -x "$F" ]]; then
    echo "Missing $F"
     exit 1
  fi
done

BUILD_DIR=${PWD}/symbolizer
mkdir -p $BUILD_DIR
cd $BUILD_DIR

ZLIB_BUILD=${BUILD_DIR}/zlib
LIBCXX_BUILD=${BUILD_DIR}/libcxx
LLVM_BUILD=${BUILD_DIR}/llvm
SYMBOLIZER_BUILD=${BUILD_DIR}/symbolizer

FLAGS=${FLAGS:-}
ZLIB_SRC=${ZLIB_SRC:-}
TARGET_TRIPLE=$($CC -print-target-triple $FLAGS)
if [[ "$FLAGS" =~ "-m32" ]] ; then
  # Avoid new wrappers.
  FLAGS+=" -U_FILE_OFFSET_BITS"
fi
FLAGS+=" -fPIC -flto -Oz -g0 -DNDEBUG -target $TARGET_TRIPLE -Wno-unused-command-line-argument"
FLAGS+=" -include ${SRC_DIR}/../sanitizer_redefine_builtins.h -DSANITIZER_COMMON_REDEFINE_BUILTINS_IN_STD -Wno-language-extension-token"

LINKFLAGS="-fuse-ld=lld -target $TARGET_TRIPLE"

# Build zlib.
if [[ ! -d ${ZLIB_BUILD} ]]; then
  if [[ -z "${ZLIB_SRC}" ]]; then
    git clone https://github.com/madler/zlib ${ZLIB_BUILD}
  else
    ZLIB_SRC=$(readlink -f $ZLIB_SRC)
    mkdir -p ${ZLIB_BUILD}
    cp -r ${ZLIB_SRC}/* ${ZLIB_BUILD}/
  fi
fi

cd ${ZLIB_BUILD}
AR="${AR}" CC="${CC}" CFLAGS="$FLAGS -Wno-deprecated-non-prototype" RANLIB=/bin/true ./configure --static
make -j libz.a

# Build and install libcxxabi and libcxx.
if [[ ! -f ${LLVM_BUILD}/build.ninja ]]; then
  rm -rf ${LIBCXX_BUILD}
  mkdir -p ${LIBCXX_BUILD}
  cd ${LIBCXX_BUILD}
  LIBCXX_FLAGS="${FLAGS} -Wno-macro-redefined"
  cmake -GNinja \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_WORKS=ON \
    -DCMAKE_CXX_COMPILER_WORKS=ON \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DLIBCXX_ABI_NAMESPACE=__InternalSymbolizer \
    '-DLIBCXX_EXTRA_SITE_DEFINES=_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS;_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS' \
    -DCMAKE_C_FLAGS_RELEASE="${LIBCXX_FLAGS}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${LIBCXX_FLAGS}" \
    -DLIBCXXABI_ENABLE_ASSERTIONS=OFF \
    -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF \
    -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
    -DLIBCXX_ENABLE_ASSERTIONS=OFF \
    -DLIBCXX_ENABLE_EXCEPTIONS=OFF \
    -DLIBCXX_ENABLE_RTTI=OFF \
    -DCMAKE_SHARED_LINKER_FLAGS="$LINKFLAGS" \
    -DLIBCXX_ENABLE_SHARED=OFF \
    -DLIBCXXABI_ENABLE_SHARED=OFF \
  $LLVM_SRC/../runtimes
fi
cd ${LIBCXX_BUILD}
ninja cxx cxxabi

FLAGS="${FLAGS} -fno-rtti -fno-exceptions"
LLVM_CFLAGS="${FLAGS} -Wno-global-constructors"
LLVM_CXXFLAGS="${LLVM_CFLAGS} -nostdinc++ -I${ZLIB_BUILD} -isystem ${LIBCXX_BUILD}/include -isystem ${LIBCXX_BUILD}/include/c++/v1"

# Build LLVM.
if [[ ! -f ${LLVM_BUILD}/build.ninja ]]; then
  rm -rf ${LLVM_BUILD}
  mkdir -p ${LLVM_BUILD}
  cd ${LLVM_BUILD}
  cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_WORKS=ON \
    -DCMAKE_CXX_COMPILER_WORKS=ON \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX \
    -DLLVM_ENABLE_LIBCXX=ON \
    -DCMAKE_C_FLAGS_RELEASE="${LLVM_CFLAGS}" \
    -DCMAKE_CXX_FLAGS_RELEASE="${LLVM_CXXFLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="$LINKFLAGS -stdlib=libc++ -L${LIBCXX_BUILD}/lib" \
    -DLLVM_TABLEGEN=$TBLGEN \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_ENABLE_ZLIB=ON \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DLLVM_ENABLE_THREADS=OFF \
  $LLVM_SRC
fi
cd ${LLVM_BUILD}
ninja LLVMSymbolize LLVMObject LLVMBinaryFormat LLVMDebugInfoDWARF LLVMSupport LLVMDebugInfoPDB LLVMDebuginfod LLVMMC LLVMDemangle LLVMTextAPI LLVMTargetParser LLVMCore

cd ${BUILD_DIR}
rm -rf ${SYMBOLIZER_BUILD}
mkdir ${SYMBOLIZER_BUILD}
cd ${SYMBOLIZER_BUILD}

echo "Compiling..."
SYMBOLIZER_FLAGS="$LLVM_CXXFLAGS -I${LLVM_SRC}/include -I${LLVM_BUILD}/include -std=c++17"
$CXX $SYMBOLIZER_FLAGS ${SRC_DIR}/sanitizer_symbolize.cpp ${SRC_DIR}/sanitizer_wrappers.cpp -c
$AR rc symbolizer.a sanitizer_symbolize.o sanitizer_wrappers.o

SYMBOLIZER_API_LIST=__sanitizer_symbolize_code
SYMBOLIZER_API_LIST+=,__sanitizer_symbolize_data
SYMBOLIZER_API_LIST+=,__sanitizer_symbolize_frame
SYMBOLIZER_API_LIST+=,__sanitizer_symbolize_flush
SYMBOLIZER_API_LIST+=,__sanitizer_symbolize_demangle
SYMBOLIZER_API_LIST+=,__sanitizer_symbolize_set_demangle
SYMBOLIZER_API_LIST+=,__sanitizer_symbolize_set_inline_frames

LIBCXX_ARCHIVE_DIR=$(dirname $(find $LIBCXX_BUILD -name libc++.a | head -n1))

# Merge all the object files together and copy the resulting library back.
$LINK $LIBCXX_ARCHIVE_DIR/libc++.a \
      $LIBCXX_ARCHIVE_DIR/libc++abi.a \
      $LLVM_BUILD/lib/libLLVMSymbolize.a \
      $LLVM_BUILD/lib/libLLVMObject.a \
      $LLVM_BUILD/lib/libLLVMBinaryFormat.a \
      $LLVM_BUILD/lib/libLLVMDebugInfoDWARF.a \
      $LLVM_BUILD/lib/libLLVMSupport.a \
      $LLVM_BUILD/lib/libLLVMDebugInfoPDB.a \
      $LLVM_BUILD/lib/libLLVMDebugInfoMSF.a \
      $LLVM_BUILD/lib/libLLVMDebugInfoCodeView.a \
      $LLVM_BUILD/lib/libLLVMDebuginfod.a \
      $LLVM_BUILD/lib/libLLVMDemangle.a \
      $LLVM_BUILD/lib/libLLVMMC.a \
      $LLVM_BUILD/lib/libLLVMTextAPI.a \
      $LLVM_BUILD/lib/libLLVMTargetParser.a \
      $LLVM_BUILD/lib/libLLVMCore.a \
      $ZLIB_BUILD/libz.a \
      symbolizer.a \
      -ignore-non-bitcode -o all.bc

echo "Optimizing..."
$OPT -passes=internalize -internalize-public-api-list=${SYMBOLIZER_API_LIST} all.bc -o opt.bc
$CC $FLAGS -fno-lto -c opt.bc -o symbolizer.o

echo "Checking undefined symbols..."
export LC_ALL=C
nm -f posix -g symbolizer.o | cut -f 1,2 -d \  | sort -u > undefined.new
grep -Ev "^#|^$" $SCRIPT_DIR/global_symbols.txt | sort -u > expected.new
(diff -u expected.new undefined.new | grep -E "^\+[^+]") && \
  (echo "Failed: unexpected symbols"; exit 1)

cp -f symbolizer.o $OUTPUT

echo "Success!"
