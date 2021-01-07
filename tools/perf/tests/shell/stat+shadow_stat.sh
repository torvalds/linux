#!/bin/sh
# perf stat metrics (shadow stat) test
# SPDX-License-Identifier: GPL-2.0

set -e

# skip if system-wide mode is forbidden
perf stat -a true > /dev/null 2>&1 || exit 2

test_global_aggr()
{
	local cyc

	perf stat -a --no-big-num -e cycles,instructions sleep 1  2>&1 | \
	grep -e cycles -e instructions | \
	while read num evt hash ipc rest
	do
		# skip not counted events
		if [[ $num == "<not" ]]; then
			continue
		fi

		# save cycles count
		if [[ $evt == "cycles" ]]; then
			cyc=$num
			continue
		fi

		# skip if no cycles
		if [[ -z $cyc ]]; then
			continue
		fi

		# use printf for rounding and a leading zero
		local res=`printf "%.2f" $(echo "scale=6; $num / $cyc" | bc -q)`
		if [[ $ipc != $res ]]; then
			echo "IPC is different: $res != $ipc  ($num / $cyc)"
			exit 1
		fi
	done
}

test_no_aggr()
{
	declare -A results

	perf stat -a -A --no-big-num -e cycles,instructions sleep 1  2>&1 | \
	grep ^CPU | \
	while read cpu num evt hash ipc rest
	do
		# skip not counted events
		if [[ $num == "<not" ]]; then
			continue
		fi

		# save cycles count
		if [[ $evt == "cycles" ]]; then
			results[$cpu]=$num
			continue
		fi

		# skip if no cycles
		local cyc=${results[$cpu]}
		if [[ -z $cyc ]]; then
			continue
		fi

		# use printf for rounding and a leading zero
		local res=`printf "%.2f" $(echo "scale=6; $num / $cyc" | bc -q)`
		if [[ $ipc != $res ]]; then
			echo "IPC is different for $cpu: $res != $ipc  ($num / $cyc)"
			exit 1
		fi
	done
}

test_global_aggr
test_no_aggr

exit 0
