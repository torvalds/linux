#!/bin/sh -e
# CoreSight / ASM Pure Loop (exclusive)

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="asm_pure_loop"

# shellcheck source=../lib/coresight.sh
. "$(dirname $0)"/../lib/coresight.sh

ARGS=""
DATV="out"
# shellcheck disable=SC2153
DATA="$DATD/perf-$TEST-$DATV.data"

perf record $PERFRECOPT -o "$DATA" "$BIN" $ARGS

perf_dump_aux_verify "$DATA" 10 10 10

err=$?
exit $err
