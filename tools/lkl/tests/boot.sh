#!/usr/bin/env bash

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
source $script_dir/test.sh

lkl_test_exec $script_dir/boot
