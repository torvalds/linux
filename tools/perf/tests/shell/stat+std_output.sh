#!/bin/bash
# perf stat STD output linter
# SPDX-License-Identifier: GPL-2.0
# Tests various perf stat STD output commands for
# default event and metricgroup

set -e

# shellcheck source=lib/stat_output.sh
. "$(dirname $0)"/lib/stat_output.sh

stat_output=$(mktemp /tmp/__perf_test.stat_output.std.XXXXX)

event_name=(cpu-clock task-clock context-switches cpu-migrations page-faults stalled-cycles-frontend stalled-cycles-backend cycles instructions branches branch-misses)
event_metric=("CPUs utilized" "CPUs utilized" "/sec" "/sec" "/sec" "frontend cycles idle" "backend cycles idle" "GHz" "insn per cycle" "/sec" "of all branches")
skip_metric=("stalled cycles per insn" "tma_" "retiring" "frontend_bound" "bad_speculation" "backend_bound" "TopdownL1" "percent of slots")

cleanup() {
  rm -f "${stat_output}"

  trap - EXIT TERM INT
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup EXIT TERM INT

function commachecker()
{
	local prefix=1

	case "$1"
	in "--interval")	prefix=2
	;; "--per-thread")	prefix=2
	;; "--system-wide-no-aggr")	prefix=2
	;; "--per-core")	prefix=3
	;; "--per-socket")	prefix=3
	;; "--per-node")	prefix=3
	;; "--per-die")		prefix=3
	;; "--per-cache")	prefix=3
	;; "--per-cluster")	prefix=3
	esac

	while read line
	do
		# Ignore initial "started on" comment.
		x=${line:0:1}
		[ "$x" = "#" ] && continue
		# Ignore initial blank line.
		[ "$line" = "" ] && continue
		# Ignore "Performance counter stats"
		x=${line:0:25}
		[ "$x" = "Performance counter stats" ] && continue
		# Ignore "seconds time elapsed" and break
		[[ "$line" == *"time elapsed"* ]] && break

		main_body=$(echo $line | cut -d' ' -f$prefix-)
		x=${main_body%#*}
		[ "$x" = "" ] && continue

		# Skip metrics without event name
		y=${main_body#*#}
		for i in "${!skip_metric[@]}"; do
			[[ "$y" == *"${skip_metric[$i]}"* ]] && break
		done
		[[ "$y" == *"${skip_metric[$i]}"* ]] && continue

		# Check default event
		for i in "${!event_name[@]}"; do
			[[ "$x" == *"${event_name[$i]}"* ]] && break
		done

		[[ ! "$x" == *"${event_name[$i]}"* ]] && {
			echo "Unknown event name in $line" 1>&2
			exit 1;
		}

		# Check event metric if it exists
		[[ ! "$main_body" == *"#"* ]] && continue
		[[ ! "$main_body" == *"${event_metric[$i]}"* ]] && {
			echo "wrong event metric. expected ${event_metric[$i]} in $line" 1>&2
			exit 1;
		}
	done < "${stat_output}"
	return 0
}

perf_cmd="-o ${stat_output}"

skip_test=$(check_for_topology)
check_no_args "STD" "$perf_cmd"
check_system_wide "STD" "$perf_cmd"
check_interval "STD" "$perf_cmd"
check_per_thread "STD" "$perf_cmd"
check_per_node "STD" "$perf_cmd"
if [ $skip_test -ne 1 ]
then
	check_system_wide_no_aggr "STD" "$perf_cmd"
	check_per_core "STD" "$perf_cmd"
	check_per_cache_instance "STD" "$perf_cmd"
	check_per_cluster "STD" "$perf_cmd"
	check_per_die "STD" "$perf_cmd"
	check_per_socket "STD" "$perf_cmd"
else
	echo "[Skip] Skipping tests for system_wide_no_aggr, per_core, per_die and per_socket since socket id exposed via topology is invalid"
fi
cleanup
exit 0
