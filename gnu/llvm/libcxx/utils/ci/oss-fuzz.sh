#!/bin/bash -eu

#
# This script runs the continuous fuzzing tests on OSS-Fuzz.
#

if [[ ${SANITIZER} = *undefined* ]]; then
  CXXFLAGS="${CXXFLAGS} -fsanitize=unsigned-integer-overflow -fsanitize-trap=unsigned-integer-overflow"
fi

BUILD=cxx_build_dir
INSTALL=cxx_install_dir
MONOREPO_ROOT=${PWD}

mkdir ${BUILD}
cmake -S ${MONOREPO_ROOT}/runtimes -B ${BUILD} \
      -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_INSTALL_PREFIX="${INSTALL}"
cmake --build ${BUILD} --target install-cxx-headers

for test in libcxx/test/libcxx/fuzzing/*.pass.cpp; do
    exe="$(basename ${test})"
    exe="${exe%.pass.cpp}"
    ${CXX} ${CXXFLAGS} \
        -std=c++20 \
        -DLIBCPP_OSS_FUZZ \
        -D_LIBCPP_HAS_NO_VENDOR_AVAILABILITY_ANNOTATIONS \
        -nostdinc++ -cxx-isystem ${INSTALL}/include/c++/v1 \
        -lpthread -ldl \
        -o "${OUT}/${exe}" \
        ${test} \
        ${LIB_FUZZING_ENGINE}
done
