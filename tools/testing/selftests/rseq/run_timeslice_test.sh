#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+

# Prevent GLIBC from registering RSEQ so the selftest can run in legacy
# and performance optimized mode.
GLIBC_TUNABLES="${GLIBC_TUNABLES:-}:glibc.pthread.rseq=0"
export GLIBC_TUNABLES

./check_optimized || {
    echo "Skipping optimized RSEQ mode test. Not supported";
    exit 0
}

./slice_test
