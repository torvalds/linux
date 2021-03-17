#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

srctree=$(dirname "$0")

gccplugins_dir=$($* -print-file-name=plugin)

# we need a c++ compiler that supports the designated initializer GNU extension
$HOSTCC -c -x c++ -std=gnu++98 - -fsyntax-only -I $srctree/gcc-plugins -I $gccplugins_dir/include 2>/dev/null <<EOF
#include "gcc-common.h"
class test {
public:
	int test;
} test = {
	.test = 1
};
EOF
