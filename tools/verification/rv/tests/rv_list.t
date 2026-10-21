#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
source ../tests/engine.sh
test_begin

set_timeout 30s

RVDIR=/sys/kernel/tracing/rv/

# Help and basic tests
check "verify help page" \
	"$RV --help" 0 "usage: rv command"

check "verify list subcommand help" \
	"$RV list --help" 0 "list all available monitors"

all_nested=$(grep : $RVDIR/available_monitors | cut -d: -f2 | paste -s | sed 's/\t/\\|/g')
all_non_nested=$(grep -v : $RVDIR/available_monitors | cut -d: -f2 | paste -s | sed 's/\t/\\|/g')
sched_monitors=$(grep sched: $RVDIR/available_monitors | cut -d: -f2 | paste -s | sed 's/\t/\\|/g')
description_state="[[:space:]]\+[[:print:]]\+\[\(OFF\|ON\)\]"
line_nested=" - \($all_nested\)${description_state}"
line_non_nested="\($all_non_nested\)${description_state}"

# List monitors and containers
check "list all monitors" \
	"$RV list" 0 "" "" "^\($line_nested\|$line_non_nested\)$"

check_if_exists "list container" \
	"$RV list sched" "$RVDIR/monitors/sched" \
	"" "-- No monitor found in container sched --" \
	"^\($sched_monitors\)${description_state}$"

check_if_exists "list non-container" \
	"$RV list wwnr" "$RVDIR/monitors/wwnr" \
	"-- No monitor found in container wwnr --" \
	"^\( - \)\?[[:alnum:]]\+${description_state}$"

check "list incomplete container name" \
	"$RV list s" 0 "-- No monitor found in container s --"

# Error handling tests
check "no command" \
	"$RV" 1 "rv requires a command"

check "invalid command" \
	"$RV invalid" 1 "rv does not know the invalid command"

test_end
