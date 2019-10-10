#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Tests the printf infrastructure using test_printf kernel module.
$(dirname $0)/../kselftest_module.sh "printf" test_printf
