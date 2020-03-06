#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
self.flags = flags

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4


if [ -f /dev/tpm0 ] ; then
	python -m unittest -v tpm2_tests.SmokeTest
	python -m unittest -v tpm2_tests.AsyncTest
else
	exit $ksft_skip
fi

CLEAR_CMD=$(which tpm2_clear)
if [ -n $CLEAR_CMD ]; then
	tpm2_clear -T device
fi
