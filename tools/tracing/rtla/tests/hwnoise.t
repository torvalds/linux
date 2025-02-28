#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source tests/engine.sh
test_begin

set_timeout 2m

check "verify help page" \
	"hwnoise --help"
check "detect noise higher than one microsecond" \
	"hwnoise -c 0 -T 1 -d 5s -q"
check "set the automatic trace mode" \
	"hwnoise -a 5 -d 30s"
check "set scheduling param to the osnoise tracer threads" \
	"hwnoise -P F:1 -c 0 -r 900000 -d 1M -q"
check "stop the trace if a single sample is higher than 1 us" \
	"hwnoise -s 1 -T 1 -t -d 30s"
check "enable a trace event trigger" \
	"hwnoise -t -e osnoise:irq_noise trigger=\"hist:key=desc,duration:sort=desc,duration:vals=hitcount\" -d 1m"

test_end
