#!/bin/sh
# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

[ -e /dev/tpm0 ] || exit $ksft_skip
[ -e /dev/tpmrm0 ] || exit $ksft_skip

python3 -m unittest -v tpm2_tests.AsyncTest 2>&1
