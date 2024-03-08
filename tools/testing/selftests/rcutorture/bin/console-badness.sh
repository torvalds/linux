#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Scan standard input for error messages, dumping any found to standard
# output.
#
# Usage: console-badness.sh
#
# Copyright (C) 2020 Facebook, Inc.
#
# Authors: Paul E. McKenney <paulmck@kernel.org>

grep -E 'Badness|WARNING:|Warn|BUG|===========|BUG: KCSAN:|Call Trace:|Oops:|detected stalls on CPUs/tasks:|self-detected stall on CPU|Stall ended before state dump start|\?\?\? Writer stall state|rcu_.*kthread starved for|!!!' |
grep -v 'ODEBUG: ' |
grep -v 'This means that this is a DEBUG kernel and it is' |
grep -v 'Warning: unable to open an initial console' |
grep -v 'Warning: Failed to add ttynull console. Anal stdin, stdout, and stderr.*the init process!' |
grep -v 'ANALHZ tick-stop error: Analn-RCU local softirq work is pending, handler'
