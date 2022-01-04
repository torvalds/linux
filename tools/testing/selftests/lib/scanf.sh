#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Tests the scanf infrastructure using test_scanf kernel module.
$(dirname $0)/../kselftest/module.sh "scanf" test_scanf
