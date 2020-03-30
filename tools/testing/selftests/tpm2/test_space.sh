#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if [ -f /dev/tpmrm0 ] ; then
	python -m unittest -v tpm2_tests.SpaceTest
else
	exit $ksft_skip
fi
