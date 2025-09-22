#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

set -e

PROGNAME="$(basename "${0}")"

function error() { printf "error: %s\n" "$*"; exit 1; }

function usage() {
cat <<EOF
Usage:
${PROGNAME} [options]

[-h|--help]                  Display this help and exit.

--llvm-root <DIR>            Path to the root of the LLVM monorepo. Only the libcxx
                             and libcxxabi directories are required.

--build-dir <DIR>            Path to the directory to use for building. This will
                             contain intermediate build products.

--install-dir <DIR>          Path to the directory to install the library to.

--symbols-dir <DIR>          Path to the directory to install the .dSYM bundle to.

--architectures "<arch>..."  A whitespace separated list of architectures to build for.
                             The library will be built for each architecture independently,
                             and a universal binary containing all architectures will be
                             created from that.

--headers-only               Only install the header part of the library -- don't actually
                             build the full library.

--version X[.Y[.Z]]          The version of the library to encode in the dylib.
EOF
}

while [[ $# -gt 0 ]]; do
    case ${1} in
        -h|--help)
            usage
            exit 0
            ;;
        --llvm-root)
            llvm_root="${2}"
            shift; shift
            ;;
        --build-dir)
            build_dir="${2}"
            shift; shift
            ;;
        --symbols-dir)
            symbols_dir="${2}"
            shift; shift
            ;;
        --install-dir)
            install_dir="${2}"
            shift; shift
            ;;
        --architectures)
            architectures="${2}"
            shift; shift
            ;;
        --headers-only)
            headers_only=true
            shift
            ;;
        --version)
            version="${2}"
            shift; shift
            ;;
        *)
            error "Unknown argument '${1}'"
            ;;
    esac
done

for arg in llvm_root build_dir symbols_dir install_dir architectures version; do
    if [ -z ${!arg+x} ]; then
        error "Missing required argument '--${arg//_/-}'"
    elif [ "${!arg}" == "" ]; then
        error "Argument to --${arg//_/-} must not be empty"
    fi
done

# Allow using relative paths
function realpath() {
    if [[ $1 = /* ]]; then echo "$1"; else echo "$(pwd)/${1#./}"; fi
}
for arg in llvm_root build_dir symbols_dir install_dir; do
    path="$(realpath "${!arg}")"
    eval "${arg}=\"${path}\""
done

function step() {
    separator="$(printf "%0.s-" $(seq 1 ${#1}))"
    echo
    echo "${separator}"
    echo "${1}"
    echo "${separator}"
}

for arch in ${architectures}; do
    # Construct the target-triple that we're testing for. Otherwise, the target triple is currently detected
    # as <arch>-apple-darwin<version> instead of <arch>-apple-macosx<version>, which trips up the test suite.
    # TODO: This shouldn't be necessary anymore if `clang -print-target-triple` behaved properly, see https://llvm.org/PR61762.
    #       Then LLVM would guess the LLVM_DEFAULT_TARGET_TRIPLE properly and we wouldn't have to specify it.
    target=$(xcrun clang -arch ${arch} -xc - -### 2>&1 | grep --only-matching -E '"-triple" ".+?"' | grep --only-matching -E '"[^ ]+-apple-[^ ]+?"' | tr -d '"')

    mkdir -p "${build_dir}/${arch}"

    step "Building shims to make libc++ compatible with the system libc++ on Apple platforms when running the tests"
    shims_library="${build_dir}/${arch}/apple-system-shims.a"
    # Note that this doesn't need to match the Standard version used to build the rest of the library.
    xcrun clang++ -c -std=c++2b -target ${target} "${llvm_root}/libcxxabi/src/vendor/apple/shims.cpp" -static -o "${shims_library}"

    step "Building libc++.dylib and libc++abi.dylib for architecture ${arch}"
    xcrun cmake -S "${llvm_root}/runtimes" \
                -B "${build_dir}/${arch}" \
                -GNinja \
                -DCMAKE_MAKE_PROGRAM="$(xcrun --find ninja)" \
                -C "${llvm_root}/libcxx/cmake/caches/Apple.cmake" \
                -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
                -DCMAKE_INSTALL_PREFIX="${build_dir}/${arch}-install" \
                -DCMAKE_INSTALL_NAME_DIR="/usr/lib" \
                -DCMAKE_OSX_ARCHITECTURES="${arch}" \
                -DLIBCXXABI_LIBRARY_VERSION="${version}" \
                -DLIBCXX_LIBRARY_VERSION="${version}" \
                -DLIBCXX_TEST_PARAMS="target_triple=${target};apple_system_shims=${shims_library}" \
                -DLIBCXXABI_TEST_PARAMS="target_triple=${target};apple_system_shims=${shims_library}" \
                -DLIBUNWIND_TEST_PARAMS="target_triple=${target};apple_system_shims=${shims_library}"

    if [ "$headers_only" = true ]; then
        xcrun cmake --build "${build_dir}/${arch}" --target install-cxx-headers install-cxxabi-headers -- -v
    else
        xcrun cmake --build "${build_dir}/${arch}" --target install-cxx install-cxxabi -- -v
    fi
done

function universal_dylib() {
    dylib=${1}

    inputs=$(for arch in ${architectures}; do echo "${build_dir}/${arch}-install/lib/${dylib}"; done)

    step "Creating a universal dylib ${dylib} from the dylibs for all architectures"
    xcrun lipo -create ${inputs} -output "${build_dir}/${dylib}"

    step "Installing the (stripped) universal dylib to ${install_dir}/usr/lib"
    mkdir -p "${install_dir}/usr/lib"
    cp "${build_dir}/${dylib}" "${install_dir}/usr/lib/${dylib}"
    xcrun strip -S "${install_dir}/usr/lib/${dylib}"

    step "Installing the unstripped dylib and the dSYM bundle to ${symbols_dir}"
    xcrun dsymutil "${build_dir}/${dylib}" -o "${symbols_dir}/${dylib}.dSYM"
    cp "${build_dir}/${dylib}" "${symbols_dir}/${dylib}"
}

if [ "$headers_only" != true ]; then
    universal_dylib libc++.1.dylib
    universal_dylib libc++abi.dylib
    (cd "${install_dir}/usr/lib" && ln -s "libc++.1.dylib" libc++.dylib)

    experimental_libs=$(for arch in ${architectures}; do echo "${build_dir}/${arch}-install/lib/libc++experimental.a"; done)
    xcrun lipo -create ${experimental_libs} -output "${install_dir}/usr/lib/libc++experimental.a"
fi

# Install the headers by copying the headers from one of the built architectures
# into the install directory. Headers from all architectures should be the same.
step "Installing the libc++ and libc++abi headers to ${install_dir}/usr/include"
any_arch=$(echo ${architectures} | cut -d ' ' -f 1)
mkdir -p "${install_dir}/usr/include"
ditto "${build_dir}/${any_arch}-install/include" "${install_dir}/usr/include"
if [[ $EUID -eq 0 ]]; then # Only chown if we're running as root
    chown -R root:wheel "${install_dir}/usr/include"
fi

if [ "$headers_only" != true ]; then
    step "Installing the libc++ and libc++abi licenses"
    mkdir -p "${install_dir}/usr/local/OpenSourceLicenses"
    cp "${llvm_root}/libcxx/LICENSE.TXT" "${install_dir}/usr/local/OpenSourceLicenses/libcxx.txt"
    cp "${llvm_root}/libcxxabi/LICENSE.TXT" "${install_dir}/usr/local/OpenSourceLicenses/libcxxabi.txt"

    # Also install universal static archives for libc++ and libc++abi
    libcxx_archives=$(for arch in ${architectures}; do echo "${build_dir}/${arch}-install/lib/libc++.a"; done)
    libcxxabi_archives=$(for arch in ${architectures}; do echo "${build_dir}/${arch}-install/lib/libc++abi.a"; done)
    step "Creating universal static archives for libc++ and libc++abi from the static archives for each architecture"
    mkdir -p "${install_dir}/usr/local/lib/libcxx"
    xcrun libtool -static ${libcxx_archives} -o "${install_dir}/usr/local/lib/libcxx/libc++-static.a"
    xcrun libtool -static ${libcxxabi_archives} -o "${install_dir}/usr/local/lib/libcxx/libc++abi-static.a"
fi
