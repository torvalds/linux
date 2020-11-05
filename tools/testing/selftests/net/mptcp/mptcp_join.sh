#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ret=0
sin=""
sout=""
cin=""
cout=""
ksft_skip=4
timeout=30
mptcp_connect=""
capture=0

TEST_COUNT=0

# generated using "nfbpf_compile '(ip && (ip[54] & 0xf0) == 0x30) ||
#				  (ip6 && (ip6[74] & 0xf0) == 0x30)'"
CBPF_MPTCP_SUBOPTION_ADD_ADDR="14,
			       48 0 0 0,
			       84 0 0 240,
			       21 0 3 64,
			       48 0 0 54,
			       84 0 0 240,
			       21 6 7 48,
			       48 0 0 0,
			       84 0 0 240,
			       21 0 4 96,
			       48 0 0 74,
			       84 0 0 240,
			       21 0 1 48,
			       6 0 0 65535,
			       6 0 0 0"

init()
{
	capout=$(mktemp)

	rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)

	ns1="ns1-$rndh"
	ns2="ns2-$rndh"

	for netns in "$ns1" "$ns2";do
		ip netns add $netns || exit $ksft_skip
		ip -net $netns link set lo up
		ip netns exec $netns sysctl -q net.mptcp.enabled=1
		ip netns exec $netns sysctl -q net.ipv4.conf.all.rp_filter=0
		ip netns exec $netns sysctl -q net.ipv4.conf.default.rp_filter=0
	done

	#  ns1              ns2
	# ns1eth1    ns2eth1
	# ns1eth2    ns2eth2
	# ns1eth3    ns2eth3
	# ns1eth4    ns2eth4

	for i in `seq 1 4`; do
		ip link add ns1eth$i netns "$ns1" type veth peer name ns2eth$i netns "$ns2"
		ip -net "$ns1" addr add 10.0.$i.1/24 dev ns1eth$i
		ip -net "$ns1" addr add dead:beef:$i::1/64 dev ns1eth$i nodad
		ip -net "$ns1" link set ns1eth$i up

		ip -net "$ns2" addr add 10.0.$i.2/24 dev ns2eth$i
		ip -net "$ns2" addr add dead:beef:$i::2/64 dev ns2eth$i nodad
		ip -net "$ns2" link set ns2eth$i up

		# let $ns2 reach any $ns1 address from any interface
		ip -net "$ns2" route add default via 10.0.$i.1 dev ns2eth$i metric 10$i
	done
}

cleanup_partial()
{
	rm -f "$capout"

	for netns in "$ns1" "$ns2"; do
		ip netns del $netns
	done
}

cleanup()
{
	rm -f "$cin" "$cout"
	rm -f "$sin" "$sout"
	cleanup_partial
}

reset()
{
	cleanup_partial
	init
}

reset_with_cookies()
{
	reset

	for netns in "$ns1" "$ns2";do
		ip netns exec $netns sysctl -q net.ipv4.tcp_syncookies=2
	done
}

reset_with_add_addr_timeout()
{
	local ip="${1:-4}"
	local tables

	tables="iptables"
	if [ $ip -eq 6 ]; then
		tables="ip6tables"
	fi

	reset

	ip netns exec $ns1 sysctl -q net.mptcp.add_addr_timeout=1
	ip netns exec $ns2 $tables -A OUTPUT -p tcp \
		-m tcp --tcp-option 30 \
		-m bpf --bytecode \
		"$CBPF_MPTCP_SUBOPTION_ADD_ADDR" \
		-j DROP
}

for arg in "$@"; do
	if [ "$arg" = "-c" ]; then
		capture=1
	fi
done

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

iptables -V > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run all tests without iptables tool"
	exit $ksft_skip
fi

ip6tables -V > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run all tests without ip6tables tool"
	exit $ksft_skip
fi

check_transfer()
{
	in=$1
	out=$2
	what=$3

	cmp "$in" "$out" > /dev/null 2>&1
	if [ $? -ne 0 ] ;then
		echo "[ FAIL ] $what does not match (in, out):"
		print_file_err "$in"
		print_file_err "$out"

		return 1
	fi

	return 0
}

do_ping()
{
	listener_ns="$1"
	connector_ns="$2"
	connect_addr="$3"

	ip netns exec ${connector_ns} ping -q -c 1 $connect_addr >/dev/null
	if [ $? -ne 0 ] ; then
		echo "$listener_ns -> $connect_addr connectivity [ FAIL ]" 1>&2
		ret=1
	fi
}

do_transfer()
{
	listener_ns="$1"
	connector_ns="$2"
	cl_proto="$3"
	srv_proto="$4"
	connect_addr="$5"
	rm_nr_ns1="$6"
	rm_nr_ns2="$7"
	speed="$8"

	port=$((10000+$TEST_COUNT))
	TEST_COUNT=$((TEST_COUNT+1))

	:> "$cout"
	:> "$sout"
	:> "$capout"

	if [ $capture -eq 1 ]; then
		if [ -z $SUDO_USER ] ; then
			capuser=""
		else
			capuser="-Z $SUDO_USER"
		fi

		capfile=$(printf "mp_join-%02u-%s.pcap" "$TEST_COUNT" "${listener_ns}")

		echo "Capturing traffic for test $TEST_COUNT into $capfile"
		ip netns exec ${listener_ns} tcpdump -i any -s 65535 -B 32768 $capuser -w $capfile > "$capout" 2>&1 &
		cappid=$!

		sleep 1
	fi

	if [ $speed = "fast" ]; then
		mptcp_connect="./mptcp_connect -j"
	else
		mptcp_connect="./mptcp_connect -r"
	fi

	ip netns exec ${listener_ns} $mptcp_connect -t $timeout -l -p $port -s ${srv_proto} 0.0.0.0 < "$sin" > "$sout" &
	spid=$!

	sleep 1

	ip netns exec ${connector_ns} $mptcp_connect -t $timeout -p $port -s ${cl_proto} $connect_addr < "$cin" > "$cout" &
	cpid=$!

	if [ $rm_nr_ns1 -gt 0 ]; then
		counter=1
		sleep 1

		while [ $counter -le $rm_nr_ns1 ]
		do
			ip netns exec ${listener_ns} ./pm_nl_ctl del $counter
			sleep 1
			let counter+=1
		done
	fi

	if [ $rm_nr_ns2 -gt 0 ]; then
		counter=1
		sleep 1

		while [ $counter -le $rm_nr_ns2 ]
		do
			ip netns exec ${connector_ns} ./pm_nl_ctl del $counter
			sleep 1
			let counter+=1
		done
	fi

	wait $cpid
	retc=$?
	wait $spid
	rets=$?

	if [ $capture -eq 1 ]; then
	    sleep 1
	    kill $cappid
	fi

	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " client exit code $retc, server $rets" 1>&2
		echo -e "\nnetns ${listener_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${listener_ns} ss -nita 1>&2 -o "sport = :$port"
		echo -e "\nnetns ${connector_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${connector_ns} ss -nita 1>&2 -o "dport = :$port"

		cat "$capout"
		return 1
	fi

	check_transfer $sin $cout "file received by client"
	retc=$?
	check_transfer $cin $sout "file received by server"
	rets=$?

	if [ $retc -eq 0 ] && [ $rets -eq 0 ];then
		cat "$capout"
		return 0
	fi

	cat "$capout"
	return 1
}

make_file()
{
	name=$1
	who=$2

	SIZE=1

	dd if=/dev/urandom of="$name" bs=1024 count=$SIZE 2> /dev/null
	echo -e "\nMPTCP_TEST_FILE_END_MARKER" >> "$name"

	echo "Created $name (size $SIZE KB) containing data sent by $who"
}

run_tests()
{
	listener_ns="$1"
	connector_ns="$2"
	connect_addr="$3"
	rm_nr_ns1="${4:-0}"
	rm_nr_ns2="${5:-0}"
	speed="${6:-fast}"
	lret=0

	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP ${connect_addr} \
		${rm_nr_ns1} ${rm_nr_ns2} ${speed}
	lret=$?
	if [ $lret -ne 0 ]; then
		ret=$lret
		return
	fi
}

chk_join_nr()
{
	local msg="$1"
	local syn_nr=$2
	local syn_ack_nr=$3
	local ack_nr=$4
	local count
	local dump_stats

	printf "%02u %-36s %s" "$TEST_COUNT" "$msg" "syn"
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinSynRx | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$syn_nr" ]; then
		echo "[fail] got $count JOIN[s] syn expected $syn_nr"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - synack"
	count=`ip netns exec $ns2 nstat -as | grep MPTcpExtMPJoinSynAckRx | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$syn_ack_nr" ]; then
		echo "[fail] got $count JOIN[s] synack expected $syn_ack_nr"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - ack"
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinAckRx | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$ack_nr" ]; then
		echo "[fail] got $count JOIN[s] ack expected $ack_nr"
		ret=1
		dump_stats=1
	else
		echo "[ ok ]"
	fi
	if [ "${dump_stats}" = 1 ]; then
		echo Server ns stats
		ip netns exec $ns1 nstat -as | grep MPTcp
		echo Client ns stats
		ip netns exec $ns2 nstat -as | grep MPTcp
	fi
}

chk_add_nr()
{
	local add_nr=$1
	local echo_nr=$2
	local count
	local dump_stats

	printf "%-39s %s" " " "add"
	count=`ip netns exec $ns2 nstat -as | grep MPTcpExtAddAddr | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$add_nr" ]; then
		echo "[fail] got $count ADD_ADDR[s] expected $add_nr"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - echo  "
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtEchoAdd | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$echo_nr" ]; then
		echo "[fail] got $count ADD_ADDR echo[s] expected $echo_nr"
		ret=1
		dump_stats=1
	else
		echo "[ ok ]"
	fi

	if [ "${dump_stats}" = 1 ]; then
		echo Server ns stats
		ip netns exec $ns1 nstat -as | grep MPTcp
		echo Client ns stats
		ip netns exec $ns2 nstat -as | grep MPTcp
	fi
}

chk_rm_nr()
{
	local rm_addr_nr=$1
	local rm_subflow_nr=$2
	local count
	local dump_stats

	printf "%-39s %s" " " "rm "
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtRmAddr | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$rm_addr_nr" ]; then
		echo "[fail] got $count RM_ADDR[s] expected $rm_addr_nr"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - sf    "
	count=`ip netns exec $ns2 nstat -as | grep MPTcpExtRmSubflow | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$rm_subflow_nr" ]; then
		echo "[fail] got $count RM_SUBFLOW[s] expected $rm_subflow_nr"
		ret=1
		dump_stats=1
	else
		echo "[ ok ]"
	fi

	if [ "${dump_stats}" = 1 ]; then
		echo Server ns stats
		ip netns exec $ns1 nstat -as | grep MPTcp
		echo Client ns stats
		ip netns exec $ns2 nstat -as | grep MPTcp
	fi
}

sin=$(mktemp)
sout=$(mktemp)
cin=$(mktemp)
cout=$(mktemp)
init
make_file "$cin" "client"
make_file "$sin" "server"
trap cleanup EXIT

run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "no JOIN" "0" "0" "0"

# subflow limted by client
reset
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "single subflow, limited by client" 0 0 0

# subflow limted by server
reset
ip netns exec $ns2 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "single subflow, limited by server" 1 1 0

# subflow
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "single subflow" 1 1 1

# multiple subflows
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "multiple subflows" 2 2 2

# multiple subflows limited by serverf
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "multiple subflows, limited by server" 2 2 1

# add_address, unused
reset
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "unused signal address" 0 0 0
chk_add_nr 1 1

# accept and use add_addr
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 1 1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "signal address" 1 1 1
chk_add_nr 1 1

# accept and use add_addr with an additional subflow
# note: signal address in server ns and local addresses in client ns must
# belong to different subnets or one of the listed local address could be
# used for 'add_addr' subflow
reset
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns1 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl limits 1 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "subflow and signal" 2 2 2
chk_add_nr 1 1

# accept and use add_addr with additional subflows
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 3
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns2 ./pm_nl_ctl limits 1 3
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "multiple subflows and signal" 3 3 3
chk_add_nr 1 1

# add_addr timeout
reset_with_add_addr_timeout
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 1 1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
run_tests $ns1 $ns2 10.0.1.1 0 0 slow
chk_join_nr "signal address, ADD_ADDR timeout" 1 1 1
chk_add_nr 4 0

# single subflow, remove
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1 0 1 slow
chk_join_nr "remove single subflow" 1 1 1
chk_rm_nr 1 1

# multiple subflows, remove
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1 0 2 slow
chk_join_nr "remove multiple subflows" 2 2 2
chk_rm_nr 2 2

# single address, remove
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns2 ./pm_nl_ctl limits 1 1
run_tests $ns1 $ns2 10.0.1.1 1 0 slow
chk_join_nr "remove single address" 1 1 1
chk_add_nr 1 1
chk_rm_nr 0 0

# subflow and signal, remove
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 2
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns2 ./pm_nl_ctl limits 1 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1 1 1 slow
chk_join_nr "remove subflow and signal" 2 2 2
chk_add_nr 1 1
chk_rm_nr 1 1

# subflows and signal, remove
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 3
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns2 ./pm_nl_ctl limits 1 3
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1 1 2 slow
chk_join_nr "remove subflows and signal" 3 3 3
chk_add_nr 1 1
chk_rm_nr 2 2

# single subflow, syncookies
reset_with_cookies
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "single subflow with syn cookies" 1 1 1

# multiple subflows with syn cookies
reset_with_cookies
ip netns exec $ns1 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "multiple subflows with syn cookies" 2 2 2

# multiple subflows limited by server
reset_with_cookies
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "subflows limited by server w cookies" 2 2 1

# test signal address with cookies
reset_with_cookies
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 1 1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "signal address with syn cookies" 1 1 1
chk_add_nr 1 1

# test cookie with subflow and signal
reset_with_cookies
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns1 ./pm_nl_ctl limits 0 2
ip netns exec $ns2 ./pm_nl_ctl limits 1 2
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "subflow and signal w cookies" 2 2 2
chk_add_nr 1 1

# accept and use add_addr with additional subflows
reset_with_cookies
ip netns exec $ns1 ./pm_nl_ctl limits 0 3
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns2 ./pm_nl_ctl limits 1 3
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "subflows and signal w. cookies" 3 3 3
chk_add_nr 1 1

exit $ret
