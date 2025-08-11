#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh
TESTS="
	extern_valid_ipv4
	extern_valid_ipv6
"
VERBOSE=0

################################################################################
# Utilities

run_cmd()
{
	local cmd="$1"
	local out
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		echo "COMMAND: $cmd"
		stderr=
	fi

	out=$(eval "$cmd" "$stderr")
	rc=$?
	if [ "$VERBOSE" -eq 1 ] && [ -n "$out" ]; then
		echo "    $out"
	fi

	return $rc
}

################################################################################
# Setup

setup()
{
	set -e

	setup_ns ns1 ns2

	ip -n "$ns1" link add veth0 type veth peer name veth1 netns "$ns2"
	ip -n "$ns1" link set dev veth0 up
	ip -n "$ns2" link set dev veth1 up

	ip -n "$ns1" address add 192.0.2.1/24 dev veth0
	ip -n "$ns1" address add 2001:db8:1::1/64 dev veth0 nodad
	ip -n "$ns2" address add 192.0.2.2/24 dev veth1
	ip -n "$ns2" address add 2001:db8:1::2/64 dev veth1 nodad

	ip netns exec "$ns1" sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1
	ip netns exec "$ns2" sysctl -qw net.ipv6.conf.all.keep_addr_on_down=1

	sleep 5

	set +e
}

exit_cleanup_all()
{
	cleanup_all_ns
	exit "${EXIT_STATUS}"
}

################################################################################
# Tests

extern_valid_common()
{
	local af_str=$1; shift
	local ip_addr=$1; shift
	local tbl_name=$1; shift
	local subnet=$1; shift
	local mac

	mac=$(ip -n "$ns2" -j link show dev veth1 | jq -r '.[]["address"]')

	RET=0

	# Check that simple addition works.
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_err $? "No \"extern_valid\" flag after addition"

	log_test "$af_str \"extern_valid\" flag: Add entry"

	RET=0

	# Check that an entry cannot be added with "extern_valid" flag and an
	# invalid state.
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr nud none dev veth0 extern_valid"
	check_fail $? "Managed to add an entry with \"extern_valid\" flag and an invalid state"

	log_test "$af_str \"extern_valid\" flag: Add with an invalid state"

	RET=0

	# Check that entry cannot be added with both "extern_valid" flag and
	# "use" / "managed" flag.
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid use"
	check_fail $? "Managed to add an entry with \"extern_valid\" flag and \"use\" flag"

	log_test "$af_str \"extern_valid\" flag: Add with \"use\" flag"

	RET=0

	# Check that "extern_valid" flag can be toggled using replace.
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0"
	run_cmd "ip -n $ns1 neigh replace $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_err $? "Did not manage to set \"extern_valid\" flag with replace"
	run_cmd "ip -n $ns1 neigh replace $ip_addr lladdr $mac nud stale dev veth0"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_fail $? "Did not manage to clear \"extern_valid\" flag with replace"

	log_test "$af_str \"extern_valid\" flag: Replace entry"

	RET=0

	# Check that an existing "extern_valid" entry can be marked as
	# "managed".
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh replace $ip_addr lladdr $mac nud stale dev veth0 extern_valid managed"
	check_err $? "Did not manage to add \"managed\" flag to an existing \"extern_valid\" entry"

	log_test "$af_str \"extern_valid\" flag: Replace entry with \"managed\" flag"

	RET=0

	# Check that entry cannot be replaced with "extern_valid" flag and an
	# invalid state.
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh replace $ip_addr nud none dev veth0 extern_valid"
	check_fail $? "Managed to replace an entry with \"extern_valid\" flag and an invalid state"

	log_test "$af_str \"extern_valid\" flag: Replace with an invalid state"

	RET=0

	# Check that an "extern_valid" entry is flushed when the interface is
	# put administratively down.
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 link set dev veth0 down"
	run_cmd "ip -n $ns1 link set dev veth0 up"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0"
	check_fail $? "\"extern_valid\" entry not flushed upon interface down"

	log_test "$af_str \"extern_valid\" flag: Interface down"

	RET=0

	# Check that an "extern_valid" entry is not flushed when the interface
	# loses its carrier.
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns2 link set dev veth1 down"
	run_cmd "ip -n $ns2 link set dev veth1 up"
	run_cmd "sleep 2"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0"
	check_err $? "\"extern_valid\" entry flushed upon carrier down"

	log_test "$af_str \"extern_valid\" flag: Carrier down"

	RET=0

	# Check that when entry transitions to "reachable" state it maintains
	# the "extern_valid" flag. Wait "delay_probe" seconds for ARP request /
	# NS to be sent.
	local delay_probe

	delay_probe=$(ip -n "$ns1" -j ntable show dev veth0 name "$tbl_name" | jq '.[]["delay_probe"]')
	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh replace $ip_addr lladdr $mac nud stale dev veth0 extern_valid use"
	run_cmd "sleep $((delay_probe / 1000 + 2))"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"REACHABLE\""
	check_err $? "Entry did not transition to \"reachable\" state"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_err $? "Entry did not maintain \"extern_valid\" flag after transition to \"reachable\" state"

	log_test "$af_str \"extern_valid\" flag: Transition to \"reachable\" state"

	RET=0

	# Drop all packets, trigger resolution and check that entry goes back
	# to "stale" state instead of "failed".
	local mcast_reprobes
	local retrans_time
	local ucast_probes
	local app_probes
	local probes
	local delay

	run_cmd "ip -n $ns1 neigh flush dev veth0"
	run_cmd "tc -n $ns2 qdisc add dev veth1 clsact"
	run_cmd "tc -n $ns2 filter add dev veth1 ingress proto all matchall action drop"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh replace $ip_addr lladdr $mac nud stale dev veth0 extern_valid use"
	retrans_time=$(ip -n "$ns1" -j ntable show dev veth0 name "$tbl_name" | jq '.[]["retrans"]')
	ucast_probes=$(ip -n "$ns1" -j ntable show dev veth0 name "$tbl_name" | jq '.[]["ucast_probes"]')
	app_probes=$(ip -n "$ns1" -j ntable show dev veth0 name "$tbl_name" | jq '.[]["app_probes"]')
	mcast_reprobes=$(ip -n "$ns1" -j ntable show dev veth0 name "$tbl_name" | jq '.[]["mcast_reprobes"]')
	delay=$((delay_probe + (ucast_probes + app_probes + mcast_reprobes) * retrans_time))
	run_cmd "sleep $((delay / 1000 + 2))"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"STALE\""
	check_err $? "Entry did not return to \"stale\" state"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_err $? "Entry did not maintain \"extern_valid\" flag after returning to \"stale\" state"
	probes=$(ip -n "$ns1" -j -s neigh get "$ip_addr" dev veth0 | jq '.[]["probes"]')
	if [[ $probes -eq 0 ]]; then
		check_err 1 "No probes were sent"
	fi

	log_test "$af_str \"extern_valid\" flag: Transition back to \"stale\" state"

	run_cmd "tc -n $ns2 qdisc del dev veth1 clsact"

	RET=0

	# Forced garbage collection runs whenever the number of entries is
	# larger than "thresh3" and deletes stale entries that have not been
	# updated in the last 5 seconds.
	#
	# Check that an "extern_valid" entry survives a forced garbage
	# collection. Add an entry, wait 5 seconds and add more entries than
	# "thresh3" so that forced garbage collection will run.
	#
	# Note that the garbage collection thresholds are global resources and
	# that changes in the initial namespace affect all the namespaces.
	local forced_gc_runs_t0
	local forced_gc_runs_t1
	local orig_thresh1
	local orig_thresh2
	local orig_thresh3

	run_cmd "ip -n $ns1 neigh flush dev veth0"
	orig_thresh1=$(ip -j ntable show name "$tbl_name" | jq '.[] | select(has("thresh1")) | .["thresh1"]')
	orig_thresh2=$(ip -j ntable show name "$tbl_name" | jq '.[] | select(has("thresh2")) | .["thresh2"]')
	orig_thresh3=$(ip -j ntable show name "$tbl_name" | jq '.[] | select(has("thresh3")) | .["thresh3"]')
	run_cmd "ip ntable change name $tbl_name thresh3 10 thresh2 9 thresh1 8"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh add ${subnet}3 lladdr $mac nud stale dev veth0"
	run_cmd "sleep 5"
	forced_gc_runs_t0=$(ip -j -s ntable show name "$tbl_name" | jq '.[] | select(has("forced_gc_runs")) | .["forced_gc_runs"]')
	for i in {1..20}; do
		run_cmd "ip -n $ns1 neigh add ${subnet}$((i + 4)) nud none dev veth0"
	done
	forced_gc_runs_t1=$(ip -j -s ntable show name "$tbl_name" | jq '.[] | select(has("forced_gc_runs")) | .["forced_gc_runs"]')
	if [[ $forced_gc_runs_t1 -eq $forced_gc_runs_t0 ]]; then
		check_err 1 "Forced garbage collection did not run"
	fi
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_err $? "Entry with \"extern_valid\" flag did not survive forced garbage collection"
	run_cmd "ip -n $ns1 neigh get ${subnet}3 dev veth0"
	check_fail $? "Entry without \"extern_valid\" flag survived forced garbage collection"

	log_test "$af_str \"extern_valid\" flag: Forced garbage collection"

	run_cmd "ip ntable change name $tbl_name thresh3 $orig_thresh3 thresh2 $orig_thresh2 thresh1 $orig_thresh1"

	RET=0

	# Periodic garbage collection runs every "base_reachable"/2 seconds and
	# if the number of entries is larger than "thresh1", then it deletes
	# stale entries that have not been used in the last "gc_stale" seconds.
	#
	# Check that an "extern_valid" entry survives a periodic garbage
	# collection. Add an "extern_valid" entry, add more than "thresh1"
	# regular entries, wait "base_reachable" (longer than "gc_stale")
	# seconds and check that the "extern_valid" entry was not deleted.
	#
	# Note that the garbage collection thresholds and "base_reachable" are
	# global resources and that changes in the initial namespace affect all
	# the namespaces.
	local periodic_gc_runs_t0
	local periodic_gc_runs_t1
	local orig_base_reachable
	local orig_gc_stale

	run_cmd "ip -n $ns1 neigh flush dev veth0"
	orig_thresh1=$(ip -j ntable show name "$tbl_name" | jq '.[] | select(has("thresh1")) | .["thresh1"]')
	orig_base_reachable=$(ip -j ntable show name "$tbl_name" | jq '.[] | select(has("thresh1")) | .["base_reachable"]')
	run_cmd "ip ntable change name $tbl_name thresh1 10 base_reachable 10000"
	orig_gc_stale=$(ip -n "$ns1" -j ntable show name "$tbl_name" dev veth0 | jq '.[]["gc_stale"]')
	run_cmd "ip -n $ns1 ntable change name $tbl_name dev veth0 gc_stale 1000"
	run_cmd "ip -n $ns1 neigh add $ip_addr lladdr $mac nud stale dev veth0 extern_valid"
	run_cmd "ip -n $ns1 neigh add ${subnet}3 lladdr $mac nud stale dev veth0"
	# Wait orig_base_reachable/2 for the new interval to take effect.
	run_cmd "sleep $(((orig_base_reachable / 1000) / 2 + 2))"
	for i in {1..20}; do
		run_cmd "ip -n $ns1 neigh add ${subnet}$((i + 4)) nud none dev veth0"
	done
	periodic_gc_runs_t0=$(ip -j -s ntable show name "$tbl_name" | jq '.[] | select(has("periodic_gc_runs")) | .["periodic_gc_runs"]')
	run_cmd "sleep 10"
	periodic_gc_runs_t1=$(ip -j -s ntable show name "$tbl_name" | jq '.[] | select(has("periodic_gc_runs")) | .["periodic_gc_runs"]')
	[[ $periodic_gc_runs_t1 -ne $periodic_gc_runs_t0 ]]
	check_err $? "Periodic garbage collection did not run"
	run_cmd "ip -n $ns1 neigh get $ip_addr dev veth0 | grep \"extern_valid\""
	check_err $? "Entry with \"extern_valid\" flag did not survive periodic garbage collection"
	run_cmd "ip -n $ns1 neigh get ${subnet}3 dev veth0"
	check_fail $? "Entry without \"extern_valid\" flag survived periodic garbage collection"

	log_test "$af_str \"extern_valid\" flag: Periodic garbage collection"

	run_cmd "ip -n $ns1 ntable change name $tbl_name dev veth0 gc_stale $orig_gc_stale"
	run_cmd "ip ntable change name $tbl_name thresh1 $orig_thresh1 base_reachable $orig_base_reachable"
}

extern_valid_ipv4()
{
	extern_valid_common "IPv4" 192.0.2.2 "arp_cache" 192.0.2.
}

extern_valid_ipv6()
{
	extern_valid_common "IPv6" 2001:db8:1::2 "ndisc_cache" 2001:db8:1::
}

################################################################################
# Usage

usage()
{
	cat <<EOF
usage: ${0##*/} OPTS

        -t <test>   Test(s) to run (default: all)
                    (options: $TESTS)
        -p          Pause on fail
        -v          Verbose mode (show commands and output)
EOF
}

################################################################################
# Main

while getopts ":t:pvh" opt; do
	case $opt in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		v) VERBOSE=$((VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

require_command jq

if ! ip neigh help 2>&1 | grep -q "extern_valid"; then
	echo "SKIP: iproute2 ip too old, missing \"extern_valid\" support"
	exit "$ksft_skip"
fi

trap exit_cleanup_all EXIT

for t in $TESTS
do
	setup; $t; cleanup_all_ns;
done
