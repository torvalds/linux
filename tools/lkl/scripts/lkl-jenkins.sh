#!/bin/bash

set -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
basedir=$(cd $script_dir/../../..; pwd)

export PATH=$PATH:/sbin

build_and_test()
{
    cd $basedir
    make mrproper
    cd tools/lkl
    make clean-conf
    make -j4
    make run-tests
}

build_and_test
