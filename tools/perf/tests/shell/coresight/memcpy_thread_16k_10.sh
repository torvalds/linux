#!/bin/bash -e
# CoreSight / Memcpy 16k 10 Threads (exclusive)

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="memcpy_thread"

# shellcheck source=../lib/coresight.sh
. "$(dirname $0)"/../lib/coresight.sh

ARGS="16 10 1"
DATV="16k_10"
# shellcheck disable=SC2153
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 10 10 10

err=$?
exit $err
