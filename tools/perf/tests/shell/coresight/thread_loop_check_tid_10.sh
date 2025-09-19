#!/bin/bash -e
# CoreSight / Thread Loop 10 Threads - Check TID (exclusive)

# SPDX-License-Identifier: GPL-2.0
# Carsten Haitzler <carsten.haitzler@arm.com>, 2021

TEST="thread_loop"

# shellcheck source=../lib/coresight.sh
. "$(dirname $0)"/../lib/coresight.sh

ARGS="10 1"
DATV="check-tid-10th"
# shellcheck disable=SC2153
DATA="$DATD/perf-$TEST-$DATV.data"
STDO="$DATD/perf-$TEST-$DATV.stdout"

SHOW_TID=1 perf record -s $PERFRECOPT -o "$DATA" "$BIN" $ARGS > $STDO

perf_dump_aux_tid_verify "$DATA" "$STDO"

err=$?
exit $err
