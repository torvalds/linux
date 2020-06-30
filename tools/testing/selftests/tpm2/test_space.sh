#!/bin/bash
# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

[ -f /dev/tpmrm0 ] || exit $ksft_skip

python -m unittest -v tpm2_tests.SpaceTest
