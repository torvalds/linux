#!/bin/bash
# perf stat CSV output linter
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
# Tests various perf stat CSV output commands for the
# correct number of fields and the CSV separator set to ','.

set -e

# shellcheck source=lib/stat_output.sh
. "$(dirname $0)"/lib/stat_output.sh

csv_sep=@

stat_output=$(mktemp /tmp/__perf_test.stat_output.csv.XXXXX)

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
	local -i cnt=0
	local exp=0

	case "$1"
	in "--no-args")		exp=6
	;; "--system-wide")	exp=6
	;; "--event")		exp=6
	;; "--interval")	exp=7
	;; "--per-thread")	exp=7
	;; "--system-wide-no-aggr")	exp=7
				[ "$(uname -m)" = "s390x" ] && exp='^[6-7]$'
	;; "--per-core")	exp=8
	;; "--per-socket")	exp=8
	;; "--per-node")	exp=8
	;; "--per-die")		exp=8
	;; "--per-cluster")	exp=8
	;; "--per-cache")	exp=8
	;; "--metric-only")	exp=2
	esac

	while read line
	do
		# Ignore initial "started on" comment.
		x=${line:0:1}
		[ "$x" = "#" ] && continue
		# Ignore initial blank line.
		[ "$line" = "" ] && continue

		# Count the number of commas
		x=$(echo $line | tr -d -c $csv_sep)
		cnt="${#x}"
		# echo $line $cnt
		[[ ! "$cnt" =~ $exp ]] && {
			echo "wrong number of fields. expected $exp in $line" 1>&2
			exit 1;
		}
	done < "${stat_output}"
	return 0
}

perf_cmd="-x$csv_sep -o ${stat_output}"

skip_test=$(check_for_topology)
check_no_args "CSV" "$perf_cmd"
check_system_wide "CSV" "$perf_cmd"
check_interval "CSV" "$perf_cmd"
check_event "CSV" "$perf_cmd"
check_per_thread "CSV" "$perf_cmd"
check_per_node "CSV" "$perf_cmd"
check_metric_only "CSV" "$perf_cmd"
if [ $skip_test -ne 1 ]
then
	check_system_wide_no_aggr "CSV" "$perf_cmd"
	check_per_core "CSV" "$perf_cmd"
	check_per_cache_instance "CSV" "$perf_cmd"
	check_per_cluster "CSV" "$perf_cmd"
	check_per_die "CSV" "$perf_cmd"
	check_per_socket "CSV" "$perf_cmd"
else
	echo "[Skip] Skipping tests for system_wide_no_aggr, per_core, per_die and per_socket since socket id exposed via topology is invalid"
fi
cleanup
exit 0
