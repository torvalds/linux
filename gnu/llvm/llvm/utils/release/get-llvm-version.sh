#!/usr/bin/env bash
#===-- get-llvm-version.sh - Get LLVM Version from sources -----------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===------------------------------------------------------------------------===#
#
# Extract the current LLVM version from the CMake files. 
#
#===------------------------------------------------------------------------===#

cmake_file=$(dirname $0)/../../../cmake/Modules/LLVMVersion.cmake
function usage() {
    echo "usage: `basename $0`"
    echo ""
    echo "Calling this script with now options will output the full version: e.g. 19.1.0"
    echo " --cmake-file      Path to cmake file with the version (default: $cmake_file)
    echo " You can use at most one of the following options:
    echo " --major           Print the major version."
    echo " --minor           Print the minor version."
    echo " --patch           Print the patch version."
}

print=""

while [ $# -gt 0 ]; do
    case $1 in
        --cmake-file )
            shift
    	    cmake_file="$1"
    	    ;;
        --major)
            if [ -n "$print" ]; then
                echo "Only one of --major, --minor, --patch is allowed"
                exit 1
            fi
            print="major"
            ;;
        --minor)
            if [ -n "$print" ]; then
                echo "Only one of --major, --minor, --patch is allowed"
                exit 1
            fi
            print="minor"
            ;;
        --patch)
            if [ -n "$print" ]; then
                echo "Only one of --major, --minor, --patch is allowed"
                exit 1
            fi
            print="patch"
            ;;
        --help | -h | -\? )
            usage
            exit 0
            ;;
        * )
            echo "unknown option: $1"
            usage
            exit 1
            ;;
    esac
    shift
done

major=`grep -o 'LLVM_VERSION_MAJOR[[:space:]]\+\([0-9]\+\)' $cmake_file  | grep -o '[0-9]\+'`
minor=`grep -o 'LLVM_VERSION_MINOR[[:space:]]\+\([0-9]\+\)' $cmake_file  | grep -o '[0-9]\+'`
patch=`grep -o 'LLVM_VERSION_PATCH[[:space:]]\+\([0-9]\+\)' $cmake_file  | grep -o '[0-9]\+'`

case $print in
    major)
        echo "$major"
        ;;
    minor)
        echo "$minor"
        ;;
    patch)
        echo "$patch"
        ;;
    *)
        echo "$major.$minor.$patch"
        ;;
esac

