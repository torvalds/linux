#!/bin/bash
# perf stat metrics (shadow stat) test
# SPDX-License-Identifier: GPL-2.0

set -e

THRESHOLD=0.015

# skip if system-wide mode is forbidden
perf stat -a true > /dev/null 2>&1 || exit 2

# skip if on hybrid platform
perf stat -a -e cycles sleep 1 2>&1 | grep -e cpu_core && exit 2

test_global_aggr()
{
	perf stat -a --no-big-num -e cycles,instructions sleep 1  2>&1 | \
	grep -e cycles -e instructions | \
	while read num evt _ ipc rest
	do
		# skip not counted events
		if [ "$num" = "<not" ]; then
			continue
		fi

		# save cycles count
		if [ "$evt" = "cycles" ]; then
			cyc=$num
			continue
		fi

		# skip if no cycles
		if [ -z "$cyc" ]; then
			continue
		fi

		# use printf for rounding and a leading zero
		res=`echo $num $cyc | awk '{printf "%.2f", $1 / $2}'`
		if [ "$ipc" != "$res" ]; then
			# check the difference from the real result for FP imperfections
			diff=`echo $ipc $res $THRESHOLD | \
			awk '{x = ($1 - $2) < 0 ? ($2 - $1) : ($1 - $2); print (x > $3)}'`

			if [ $diff -eq 1 ]; then
				echo "IPC is different: $res != $ipc  ($num / $cyc)"
				exit 1
			fi

			echo "Warning: Difference of IPC is under the threshold"
		fi
	done
}

test_no_aggr()
{
	perf stat -a -A --no-big-num -e cycles,instructions sleep 1  2>&1 | \
	grep ^CPU | \
	while read cpu num evt _ ipc rest
	do
		# skip not counted events
		if [ "$num" = "<not" ]; then
			continue
		fi

		# save cycles count
		if [ "$evt" = "cycles" ]; then
			results="$results $cpu:$num"
			continue
		fi

		cyc=${results##* $cpu:}
		cyc=${cyc%% *}

		# skip if no cycles
		if [ -z "$cyc" ]; then
			continue
		fi

		# use printf for rounding and a leading zero
		res=`echo $num $cyc | awk '{printf "%.2f", $1 / $2}'`
		if [ "$ipc" != "$res" ]; then
			# check difference from the real result for FP imperfections
			diff=`echo $ipc $res $THRESHOLD | \
			awk '{x = ($1 - $2) < 0 ? ($2 - $1) : ($1 - $2); print (x > $3)}'`

			if [ $diff -eq 1 ]; then
				echo "IPC is different: $res != $ipc  ($num / $cyc)"
				exit 1
			fi

			echo "Warning: Difference of IPC is under the threshold"
		fi
	done
}

test_global_aggr
test_no_aggr

exit 0
