#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ret=0
sin=""
sout=""
cin=""
cout=""
ksft_skip=4
timeout=30
capture=0

TEST_COUNT=0

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

		capfile="mp_join-${listener_ns}.pcap"

		echo "Capturing traffic for test $TEST_COUNT into $capfile"
		ip netns exec ${listener_ns} tcpdump -i any -s 65535 -B 32768 $capuser -w $capfile > "$capout" 2>&1 &
		cappid=$!

		sleep 1
	fi

	ip netns exec ${listener_ns} ./mptcp_connect -j -t $timeout -l -p $port -s ${srv_proto} 0.0.0.0 < "$sin" > "$sout" &
	spid=$!

	sleep 1

	ip netns exec ${connector_ns} ./mptcp_connect -j -t $timeout -p $port -s ${cl_proto} $connect_addr < "$cin" > "$cout" &
	cpid=$!

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
		echo "\nnetns ${listener_ns} socket stat for $port:" 1>&2
		ip netns exec ${listener_ns} ss -nita 1>&2 -o "sport = :$port"
		echo "\nnetns ${connector_ns} socket stat for $port:" 1>&2
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
	lret=0

	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP ${connect_addr}
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

	printf "%-36s %s" "$msg" "syn"
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

# accept and use add_addr
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 1
ip netns exec $ns2 ./pm_nl_ctl limits 1 1
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "signal address" 1 1 1

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

# accept and use add_addr with additional subflows
reset
ip netns exec $ns1 ./pm_nl_ctl limits 0 3
ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
ip netns exec $ns2 ./pm_nl_ctl limits 1 3
ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
run_tests $ns1 $ns2 10.0.1.1
chk_join_nr "multiple subflows and signal" 3 3 3

exit $ret
