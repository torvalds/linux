#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

$* -dM -E - </dev/null 2>&1 | grep -q __ANDROID__ && echo "y"
