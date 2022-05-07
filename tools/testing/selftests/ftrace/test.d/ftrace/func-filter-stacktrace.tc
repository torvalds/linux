#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: ftrace - stacktrace filter command
# requires: set_ftrace_filter
# flags: instance

echo $FUNCTION_FORK:stacktrace >> set_ftrace_filter

grep -q "$FUNCTION_FORK:stacktrace:unlimited" set_ftrace_filter

(echo "forked"; sleep 1)

grep -q "<stack trace>" trace
