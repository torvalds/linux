#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

[ -f /dev/tpm0 ] || exit $ksft_skip

python -m unittest -v tpm2_tests.SmokeTest
python -m unittest -v tpm2_tests.AsyncTest

CLEAR_CMD=$(which tpm2_clear)
if [ -n $CLEAR_CMD ]; then
	tpm2_clear -T device
fi
