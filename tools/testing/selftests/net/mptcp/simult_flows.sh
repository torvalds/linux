#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it, especially because there were too many before having
# address all other issues detected by shellcheck.
#shellcheck disable=SC2086

. "$(dirname "${0}")/mptcp_lib.sh"

ns1=""
ns2=""
ns3=""
capture=false
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
# a bit more space: because we have more to display
MPTCP_LIB_TEST_FORMAT="%02u %-60s"
ret=0
bail=0
slack=50
large=""
small=""
sout=""
cout=""
capout=""
size=0

usage() {
	echo "Usage: $0 [ -b ] [ -c ] [ -d ] [ -i]"
	echo -e "\t-b: bail out after first error, otherwise runs all testcases"
	echo -e "\t-c: capture packets for each test using tcpdump (default: no capture)"
	echo -e "\t-d: debug this script"
	echo -e "\t-i: use 'ip mptcp' instead of 'pm_nl_ctl'"
}

# This function is used in the cleanup trap
#shellcheck disable=SC2317,SC2329
cleanup()
{
	rm -f "$cout" "$sout"
	rm -f "$large" "$small"
	rm -f "$capout"

	mptcp_lib_ns_exit "${ns1}" "${ns2}" "${ns3}"
}

mptcp_lib_check_mptcp
mptcp_lib_check_tools ip tc

#  "$ns1"              ns2                    ns3
#     ns1eth1    ns2eth1   ns2eth3      ns3eth1
#            netem
#     ns1eth2    ns2eth2
#            netem

setup()
{
	large=$(mktemp)
	small=$(mktemp)
	sout=$(mktemp)
	cout=$(mktemp)
	capout=$(mktemp)
	size=$((2 * 2048 * 4096))

	dd if=/dev/zero of=$small bs=4096 count=20 >/dev/null 2>&1
	dd if=/dev/zero of=$large bs=4096 count=$((size / 4096)) >/dev/null 2>&1

	trap cleanup EXIT

	mptcp_lib_ns_init ns1 ns2 ns3

	ip link add ns1eth1 netns "$ns1" type veth peer name ns2eth1 netns "$ns2"
	ip link add ns1eth2 netns "$ns1" type veth peer name ns2eth2 netns "$ns2"
	ip link add ns2eth3 netns "$ns2" type veth peer name ns3eth1 netns "$ns3"

	ip -net "$ns1" addr add 10.0.1.1/24 dev ns1eth1
	ip -net "$ns1" addr add dead:beef:1::1/64 dev ns1eth1 nodad
	ip -net "$ns1" link set ns1eth1 up mtu 1500
	ip -net "$ns1" route add default via 10.0.1.2
	ip -net "$ns1" route add default via dead:beef:1::2

	ip -net "$ns1" addr add 10.0.2.1/24 dev ns1eth2
	ip -net "$ns1" addr add dead:beef:2::1/64 dev ns1eth2 nodad
	ip -net "$ns1" link set ns1eth2 up mtu 1500
	ip -net "$ns1" route add default via 10.0.2.2 metric 101
	ip -net "$ns1" route add default via dead:beef:2::2 metric 101

	mptcp_lib_pm_nl_set_limits "${ns1}" 1 1
	mptcp_lib_pm_nl_add_endpoint "${ns1}" 10.0.2.1 dev ns1eth2 flags subflow

	ip -net "$ns2" addr add 10.0.1.2/24 dev ns2eth1
	ip -net "$ns2" addr add dead:beef:1::2/64 dev ns2eth1 nodad
	ip -net "$ns2" link set ns2eth1 up mtu 1500

	ip -net "$ns2" addr add 10.0.2.2/24 dev ns2eth2
	ip -net "$ns2" addr add dead:beef:2::2/64 dev ns2eth2 nodad
	ip -net "$ns2" link set ns2eth2 up mtu 1500

	ip -net "$ns2" addr add 10.0.3.2/24 dev ns2eth3
	ip -net "$ns2" addr add dead:beef:3::2/64 dev ns2eth3 nodad
	ip -net "$ns2" link set ns2eth3 up mtu 1500
	ip netns exec "$ns2" sysctl -q net.ipv4.ip_forward=1
	ip netns exec "$ns2" sysctl -q net.ipv6.conf.all.forwarding=1

	ip -net "$ns3" addr add 10.0.3.3/24 dev ns3eth1
	ip -net "$ns3" addr add dead:beef:3::3/64 dev ns3eth1 nodad
	ip -net "$ns3" link set ns3eth1 up mtu 1500
	ip -net "$ns3" route add default via 10.0.3.2
	ip -net "$ns3" route add default via dead:beef:3::2

	mptcp_lib_pm_nl_set_limits "${ns3}" 1 1

	# debug build can slow down measurably the test program
	# we use quite tight time limit on the run-time, to ensure
	# maximum B/W usage.
	# Use kmemleak/lockdep/kasan/prove_locking presence as a rough
	# estimate for this being a debug kernel and increase the
	# maximum run-time accordingly. Observed run times for CI builds
	# running selftests, including kbuild, were used to determine the
	# amount of time to add.
	grep -q ' kmemleak_init$\| lockdep_init$\| kasan_init$\| prove_locking$' /proc/kallsyms && slack=$((slack+550))
}

do_transfer()
{
	local cin=$1
	local sin=$2
	local max_time=$3
	local port
	port=$((10000+MPTCP_LIB_TEST_COUNTER))

	:> "$cout"
	:> "$sout"
	:> "$capout"

	if $capture; then
		local capuser
		local rndh="${ns1:4}"
		if [ -z $SUDO_USER ] ; then
			capuser=""
		else
			capuser="-Z $SUDO_USER"
		fi

		local capfile="${rndh}-${port}"
		local capopt="-i any -s 65535 -B 32768 ${capuser}"

		ip netns exec ${ns3}  tcpdump ${capopt} -w "${capfile}-listener.pcap"  >> "${capout}" 2>&1 &
		local cappid_listener=$!

		ip netns exec ${ns1} tcpdump ${capopt} -w "${capfile}-connector.pcap" >> "${capout}" 2>&1 &
		local cappid_connector=$!

		sleep 1
	fi

	NSTAT_HISTORY=/tmp/${ns3}.nstat ip netns exec ${ns3} \
		nstat -n
	NSTAT_HISTORY=/tmp/${ns1}.nstat ip netns exec ${ns1} \
		nstat -n

	timeout ${timeout_test} \
		ip netns exec ${ns3} \
			./mptcp_connect -jt ${timeout_poll} -l -p $port -T $max_time \
				0.0.0.0 < "$sin" > "$sout" &
	local spid=$!

	mptcp_lib_wait_local_port_listen "${ns3}" "${port}"

	timeout ${timeout_test} \
		ip netns exec ${ns1} \
			./mptcp_connect -jt ${timeout_poll} -p $port -T $max_time \
				10.0.3.3 < "$cin" > "$cout" &
	local cpid=$!

	wait $cpid
	local retc=$?
	wait $spid
	local rets=$?

	if $capture; then
		sleep 1
		kill ${cappid_listener}
		kill ${cappid_connector}
	fi

	NSTAT_HISTORY=/tmp/${ns3}.nstat ip netns exec ${ns3} \
		nstat | grep Tcp > /tmp/${ns3}.out
	NSTAT_HISTORY=/tmp/${ns1}.nstat ip netns exec ${ns1} \
		nstat | grep Tcp > /tmp/${ns1}.out

	cmp $sin $cout > /dev/null 2>&1
	local cmps=$?
	cmp $cin $sout > /dev/null 2>&1
	local cmpc=$?

	if [ $retc -eq 0 ] && [ $rets -eq 0 ] && \
	   [ $cmpc -eq 0 ] && [ $cmps -eq 0 ]; then
		printf "%-16s" " max $max_time "
		mptcp_lib_pr_ok
		cat "$capout"
		return 0
	fi

	mptcp_lib_pr_fail "client exit code $retc, server $rets"
	mptcp_lib_pr_err_stats "${ns3}" "${ns1}" "${port}" \
		"/tmp/${ns3}.out" "/tmp/${ns1}.out"
	ls -l $sin $cout
	ls -l $cin $sout

	cat "$capout"
	return 1
}

run_test()
{
	local rate1=$1
	local rate2=$2
	local delay1=$3
	local delay2=$4
	local lret
	local dev
	shift 4
	local msg=$*

	[ $delay1 -gt 0 ] && delay1="delay ${delay1}ms" || delay1=""
	[ $delay2 -gt 0 ] && delay2="delay ${delay2}ms" || delay2=""

	for dev in ns1eth1 ns1eth2; do
		tc -n $ns1 qdisc del dev $dev root >/dev/null 2>&1
	done
	for dev in ns2eth1 ns2eth2; do
		tc -n $ns2 qdisc del dev $dev root >/dev/null 2>&1
	done
	tc -n $ns1 qdisc add dev ns1eth1 root netem rate ${rate1}mbit $delay1
	tc -n $ns1 qdisc add dev ns1eth2 root netem rate ${rate2}mbit $delay2
	tc -n $ns2 qdisc add dev ns2eth1 root netem rate ${rate1}mbit $delay1
	tc -n $ns2 qdisc add dev ns2eth2 root netem rate ${rate2}mbit $delay2

	# time is measured in ms, account for transfer size, aggregated link speed
	# and header overhead (10%)
	#              ms    byte -> bit   10%        mbit      -> kbit -> bit  10%
	local time=$((1000 * size  *  8  * 10 / ((rate1 + rate2) * 1000 * 1000 * 9) ))

	# mptcp_connect will do some sleeps to allow the mp_join handshake
	# completion (see mptcp_connect): 200ms on each side, add some slack
	time=$((time + 400 + slack))

	mptcp_lib_print_title "$msg"
	do_transfer $small $large $time
	lret=$?
	mptcp_lib_result_code "${lret}" "${msg}"
	if [ $lret -ne 0 ] && ! mptcp_lib_subtest_is_flaky; then
		ret=$lret
		[ $bail -eq 0 ] || exit $ret
	fi

	msg+=" - reverse direction"
	mptcp_lib_print_title "${msg}"
	do_transfer $large $small $time
	lret=$?
	mptcp_lib_result_code "${lret}" "${msg}"
	if [ $lret -ne 0 ] && ! mptcp_lib_subtest_is_flaky; then
		ret=$lret
		[ $bail -eq 0 ] || exit $ret
	fi
}

while getopts "bcdhi" option;do
	case "$option" in
	"h")
		usage $0
		exit ${KSFT_PASS}
		;;
	"b")
		bail=1
		;;
	"c")
		capture=true
		;;
	"d")
		set -x
		;;
	"i")
		mptcp_lib_set_ip_mptcp
		;;
	"?")
		usage $0
		exit ${KSFT_FAIL}
		;;
	esac
done

setup
mptcp_lib_subtests_last_ts_reset
run_test 10 10 0 0 "balanced bwidth"
run_test 10 10 1 25 "balanced bwidth with unbalanced delay"

# we still need some additional infrastructure to pass the following test-cases
MPTCP_LIB_SUBTEST_FLAKY=1 run_test 10 3 0 0 "unbalanced bwidth"
run_test 10 3 1 25 "unbalanced bwidth with unbalanced delay"
run_test 10 3 25 1 "unbalanced bwidth with opposed, unbalanced delay"

mptcp_lib_result_print_all_tap
exit $ret
