#!/usr/bin/env bash
#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

#
# This script builds picolibc (https://github.com/picolibc/picolibc) from
# source to facilitate building libc++ against it.
#

set -e

PROGNAME="$(basename "${0}")"

function error() { printf "error: %s\n" "$*" >&2; exit 1; }

function usage() {
cat <<EOF
Usage:
${PROGNAME} [options]

[-h|--help]                  Display this help and exit.

--build-dir <DIR>            Path to the directory to use for building.

--install-dir <DIR>          Path to the directory to install the library to.
EOF
}

while [[ $# -gt 0 ]]; do
    case ${1} in
        -h|--help)
            usage
            exit 0
            ;;
        --build-dir)
            build_dir="${2}"
            shift; shift
            ;;
        --install-dir)
            install_dir="${2}"
            shift; shift
            ;;
        --target)
            target="${2}"
            shift; shift
            ;;
        *)
            error "Unknown argument '${1}'"
            ;;
    esac
done

for arg in build_dir install_dir target; do
    if [ -z ${!arg+x} ]; then
        error "Missing required argument '--${arg//_/-}'"
    elif [ "${!arg}" == "" ]; then
        error "Argument to --${arg//_/-} must not be empty"
    fi
done


echo "--- Downloading picolibc"
picolibc_source_dir="${build_dir}/picolibc-source"
picolibc_build_dir="${build_dir}/picolibc-build"
mkdir -p "${picolibc_source_dir}"
mkdir -p "${picolibc_build_dir}"
# Download the version of picolibc that was the latest at the time this script was written.
# Following changes are required and were introduced after version 1.8.5:
# - updated semihost arguments handling,
# - added missing macros in stdio.h
# - external linkage for isblank
# Version following 1.8.5, was not released by the time of writing.
picolibc_commit="04a90c56d7aac61880f205ec29b3dce6a9de0342"
curl -L "https://github.com/picolibc/picolibc/archive/${picolibc_commit}.zip" --output "${picolibc_source_dir}/picolibc.zip"
unzip -q "${picolibc_source_dir}/picolibc.zip" -d "${picolibc_source_dir}"
mv "${picolibc_source_dir}/picolibc-${picolibc_commit}"/* "${picolibc_source_dir}"
rm -rf "${picolibc_source_dir}/picolibc-${picolibc_commit}"

cat <<EOF > "${picolibc_build_dir}/meson-cross-build.txt"
[binaries]
c = ['${CC:-cc}', '--target=${target}', '-mfloat-abi=soft', '-nostdlib']
ar = 'llvm-ar'
as = 'llvm-as'
ld = 'lld'
strip = 'llvm-strip'
[host_machine]
system = 'none'
cpu_family = 'arm'
cpu = 'arm'
endian = 'little'
[properties]
skip_sanity_check = true
EOF

venv_dir="${build_dir}/meson-venv"
python3 -m venv "${venv_dir}"
# Install the version of meson that was the latest at the time this script was written.
"${venv_dir}/bin/pip" install "meson==1.1.1"

"${venv_dir}/bin/meson" setup \
  -Dincludedir=include -Dlibdir=lib -Dspecsdir=none -Dmultilib=false -Dpicoexit=false \
  --prefix "${install_dir}" \
  --cross-file "${picolibc_build_dir}/meson-cross-build.txt" \
  "${picolibc_build_dir}" \
  "${picolibc_source_dir}"

"${venv_dir}/bin/meson" install -C "${picolibc_build_dir}"
