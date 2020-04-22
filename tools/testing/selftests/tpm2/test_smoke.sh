#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

python -m unittest -v tpm2_tests.SmokeTest
python -m unittest -v tpm2_tests.AsyncTest

CLEAR_CMD=$(which tpm2_clear)
if [ -n $CLEAR_CMD ]; then
	tpm2_clear -T device
fi
