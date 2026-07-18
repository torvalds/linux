#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

GLIBC_TUNABLES="${GLIBC_TUNABLES:-}:glibc.pthread.rseq=0" ./legacy_check
