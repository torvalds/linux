#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source tests/engine.sh
test_begin

set_timeout 2m

check "verify help page" \
	"hwnoise --help" 0 "summary of hardware-related noise"
check "detect noise higher than one microsecond" \
	"hwnoise -c 0 -T 1 -d 5s -q" 0
check "set the automatic trace mode" \
	"hwnoise -a 5 -d 10s" 2 "osnoise hit stop tracing"
check "set scheduling param to the osnoise tracer threads" \
	"hwnoise -P F:1 -c 0 -r 900000 -d 10s -q"
check "stop the trace if a single sample is higher than 1 us" \
	"hwnoise -s 1 -T 1 -t -d 10s" 2 "Saving trace to osnoise_trace.txt"
check "enable a trace event trigger" \
	"hwnoise -t -e osnoise:irq_noise --trigger=\"hist:key=desc,duration:sort=desc,duration:vals=hitcount\" -d 10s" \
	0 "Saving event osnoise:irq_noise hist to osnoise_irq_noise_hist.txt"

test_end
