#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Selftest for kselftest_harness.h
#

set -e

DIR="$(dirname $(readlink -f "$0"))"

"$DIR"/harness-selftest > harness-selftest.seen || true

diff -u "$DIR"/harness-selftest.expected harness-selftest.seen
