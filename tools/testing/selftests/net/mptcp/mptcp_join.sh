#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ret=0
sin=""
sout=""
cin=""
cinsent=""
cout=""
ksft_skip=4
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
mptcp_connect=""
capture=0
checksum=0
do_all_tests=1

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
		if [ $checksum -eq 1 ]; then
			ip netns exec $netns sysctl -q net.mptcp.checksum_enabled=1
		fi
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
		rm -f /tmp/$netns.{nstat,out}
	done
}

cleanup()
{
	rm -f "$cin" "$cout"
	rm -f "$sin" "$sout" "$cinsent"
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

reset_with_checksum()
{
	local ns1_enable=$1
	local ns2_enable=$2

	reset

	ip netns exec $ns1 sysctl -q net.mptcp.checksum_enabled=$ns1_enable
	ip netns exec $ns2 sysctl -q net.mptcp.checksum_enabled=$ns2_enable
}

reset_with_allow_join_id0()
{
	local ns1_enable=$1
	local ns2_enable=$2

	reset

	ip netns exec $ns1 sysctl -q net.mptcp.allow_join_initial_addr_port=$ns1_enable
	ip netns exec $ns2 sysctl -q net.mptcp.allow_join_initial_addr_port=$ns2_enable
}

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

print_file_err()
{
	ls -l "$1" 1>&2
	echo "Trailing bytes are: "
	tail -c 27 "$1"
}

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
		ret=1

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

link_failure()
{
	ns="$1"

	l=$((RANDOM%4))
	l=$((l+1))

	veth="ns1eth$l"
	ip -net "$ns" link set "$veth" down
}

# $1: IP address
is_v6()
{
	[ -z "${1##*:*}" ]
}

do_transfer()
{
	listener_ns="$1"
	connector_ns="$2"
	cl_proto="$3"
	srv_proto="$4"
	connect_addr="$5"
	test_link_fail="$6"
	addr_nr_ns1="$7"
	addr_nr_ns2="$8"
	speed="$9"
	bkup="${10}"

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

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat -n
	NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
		nstat -n

	if [ $speed = "fast" ]; then
		mptcp_connect="./mptcp_connect -j"
	elif [ $speed = "slow" ]; then
		mptcp_connect="./mptcp_connect -r 50"
	elif [ $speed = "least" ]; then
		mptcp_connect="./mptcp_connect -r 10"
	fi

	local local_addr
	if is_v6 "${connect_addr}"; then
		local_addr="::"
	else
		local_addr="0.0.0.0"
	fi

	timeout ${timeout_test} \
		ip netns exec ${listener_ns} \
			$mptcp_connect -t ${timeout_poll} -l -p $port -s ${srv_proto} \
				${local_addr} < "$sin" > "$sout" &
	spid=$!

	sleep 1

	if [ "$test_link_fail" -eq 0 ];then
		timeout ${timeout_test} \
			ip netns exec ${connector_ns} \
				$mptcp_connect -t ${timeout_poll} -p $port -s ${cl_proto} \
					$connect_addr < "$cin" > "$cout" &
	else
		( cat "$cin" ; sleep 2; link_failure $listener_ns ; cat "$cin" ) | \
			tee "$cinsent" | \
			timeout ${timeout_test} \
				ip netns exec ${connector_ns} \
					$mptcp_connect -t ${timeout_poll} -p $port -s ${cl_proto} \
						$connect_addr > "$cout" &
	fi
	cpid=$!

	if [ $addr_nr_ns1 -gt 0 ]; then
		let add_nr_ns1=addr_nr_ns1
		counter=2
		sleep 1
		while [ $add_nr_ns1 -gt 0 ]; do
			local addr
			if is_v6 "${connect_addr}"; then
				addr="dead:beef:$counter::1"
			else
				addr="10.0.$counter.1"
			fi
			ip netns exec $ns1 ./pm_nl_ctl add $addr flags signal
			let counter+=1
			let add_nr_ns1-=1
		done
		sleep 1
	elif [ $addr_nr_ns1 -lt 0 ]; then
		let rm_nr_ns1=-addr_nr_ns1
		if [ $rm_nr_ns1 -lt 8 ]; then
			counter=1
			dump=(`ip netns exec ${listener_ns} ./pm_nl_ctl dump`)
			if [ ${#dump[@]} -gt 0 ]; then
				id=${dump[1]}
				sleep 1

				while [ $counter -le $rm_nr_ns1 ]
				do
					ip netns exec ${listener_ns} ./pm_nl_ctl del $id
					sleep 1
					let counter+=1
					let id+=1
				done
			fi
		elif [ $rm_nr_ns1 -eq 8 ]; then
			sleep 1
			ip netns exec ${listener_ns} ./pm_nl_ctl flush
		elif [ $rm_nr_ns1 -eq 9 ]; then
			sleep 1
			ip netns exec ${listener_ns} ./pm_nl_ctl del 0 ${connect_addr}
		fi
	fi

	if [ $addr_nr_ns2 -gt 0 ]; then
		let add_nr_ns2=addr_nr_ns2
		counter=3
		sleep 1
		while [ $add_nr_ns2 -gt 0 ]; do
			local addr
			if is_v6 "${connect_addr}"; then
				addr="dead:beef:$counter::2"
			else
				addr="10.0.$counter.2"
			fi
			ip netns exec $ns2 ./pm_nl_ctl add $addr flags subflow
			let counter+=1
			let add_nr_ns2-=1
		done
		sleep 1
	elif [ $addr_nr_ns2 -lt 0 ]; then
		let rm_nr_ns2=-addr_nr_ns2
		if [ $rm_nr_ns2 -lt 8 ]; then
			counter=1
			dump=(`ip netns exec ${connector_ns} ./pm_nl_ctl dump`)
			if [ ${#dump[@]} -gt 0 ]; then
				id=${dump[1]}
				sleep 1

				while [ $counter -le $rm_nr_ns2 ]
				do
					ip netns exec ${connector_ns} ./pm_nl_ctl del $id
					sleep 1
					let counter+=1
					let id+=1
				done
			fi
		elif [ $rm_nr_ns2 -eq 8 ]; then
			sleep 1
			ip netns exec ${connector_ns} ./pm_nl_ctl flush
		elif [ $rm_nr_ns2 -eq 9 ]; then
			local addr
			if is_v6 "${connect_addr}"; then
				addr="dead:beef:1::2"
			else
				addr="10.0.1.2"
			fi
			sleep 1
			ip netns exec ${connector_ns} ./pm_nl_ctl del 0 $addr
		fi
	fi

	if [ ! -z $bkup ]; then
		sleep 1
		for netns in "$ns1" "$ns2"; do
			dump=(`ip netns exec $netns ./pm_nl_ctl dump`)
			if [ ${#dump[@]} -gt 0 ]; then
				addr=${dump[${#dump[@]} - 1]}
				backup="ip netns exec $netns ./pm_nl_ctl set $addr flags $bkup"
				$backup
			fi
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

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat | grep Tcp > /tmp/${listener_ns}.out
	NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
		nstat | grep Tcp > /tmp/${connector_ns}.out

	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo " client exit code $retc, server $rets" 1>&2
		echo -e "\nnetns ${listener_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${listener_ns} ss -Menita 1>&2 -o "sport = :$port"
		cat /tmp/${listener_ns}.out
		echo -e "\nnetns ${connector_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${connector_ns} ss -Menita 1>&2 -o "dport = :$port"
		cat /tmp/${connector_ns}.out

		cat "$capout"
		ret=1
		return 1
	fi

	check_transfer $sin $cout "file received by client"
	retc=$?
	if [ "$test_link_fail" -eq 0 ];then
		check_transfer $cin $sout "file received by server"
	else
		check_transfer $cinsent $sout "file received by server"
	fi
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
	size=$3

	dd if=/dev/urandom of="$name" bs=1024 count=$size 2> /dev/null
	echo -e "\nMPTCP_TEST_FILE_END_MARKER" >> "$name"

	echo "Created $name (size $size KB) containing data sent by $who"
}

run_tests()
{
	listener_ns="$1"
	connector_ns="$2"
	connect_addr="$3"
	test_linkfail="${4:-0}"
	addr_nr_ns1="${5:-0}"
	addr_nr_ns2="${6:-0}"
	speed="${7:-fast}"
	bkup="${8:-""}"
	lret=0
	oldin=""

	if [ "$test_linkfail" -eq 1 ];then
		size=$((RANDOM%1024))
		size=$((size+1))
		size=$((size*128))

		oldin=$(mktemp)
		cp "$cin" "$oldin"
		make_file "$cin" "client" $size
	fi

	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP ${connect_addr} \
		${test_linkfail} ${addr_nr_ns1} ${addr_nr_ns2} ${speed} ${bkup}
	lret=$?

	if [ "$test_linkfail" -eq 1 ];then
		cp "$oldin" "$cin"
		rm -f "$oldin"
	fi

	if [ $lret -ne 0 ]; then
		ret=$lret
		return
	fi
}

chk_csum_nr()
{
	local msg=${1:-""}
	local count
	local dump_stats

	if [ ! -z "$msg" ]; then
		printf "%02u" "$TEST_COUNT"
	else
		echo -n "  "
	fi
	printf " %-36s %s" "$msg" "sum"
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtDataCsumErr | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != 0 ]; then
		echo "[fail] got $count data checksum error[s] expected 0"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi
	echo -n " - csum  "
	count=`ip netns exec $ns2 nstat -as | grep MPTcpExtDataCsumErr | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != 0 ]; then
		echo "[fail] got $count data checksum error[s] expected 0"
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
	if [ $checksum -eq 1 ]; then
		chk_csum_nr
	fi
}

chk_add_nr()
{
	local add_nr=$1
	local echo_nr=$2
	local port_nr=${3:-0}
	local syn_nr=${4:-$port_nr}
	local syn_ack_nr=${5:-$port_nr}
	local ack_nr=${6:-$port_nr}
	local mis_syn_nr=${7:-0}
	local mis_ack_nr=${8:-0}
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
		echo -n "[ ok ]"
	fi

	if [ $port_nr -gt 0 ]; then
		echo -n " - pt "
		count=`ip netns exec $ns2 nstat -as | grep MPTcpExtPortAdd | awk '{print $2}'`
		[ -z "$count" ] && count=0
		if [ "$count" != "$port_nr" ]; then
			echo "[fail] got $count ADD_ADDR[s] with a port-number expected $port_nr"
			ret=1
			dump_stats=1
		else
			echo "[ ok ]"
		fi

		printf "%-39s %s" " " "syn"
		count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinPortSynRx |
			awk '{print $2}'`
		[ -z "$count" ] && count=0
		if [ "$count" != "$syn_nr" ]; then
			echo "[fail] got $count JOIN[s] syn with a different \
				port-number expected $syn_nr"
			ret=1
			dump_stats=1
		else
			echo -n "[ ok ]"
		fi

		echo -n " - synack"
		count=`ip netns exec $ns2 nstat -as | grep MPTcpExtMPJoinPortSynAckRx |
			awk '{print $2}'`
		[ -z "$count" ] && count=0
		if [ "$count" != "$syn_ack_nr" ]; then
			echo "[fail] got $count JOIN[s] synack with a different \
				port-number expected $syn_ack_nr"
			ret=1
			dump_stats=1
		else
			echo -n "[ ok ]"
		fi

		echo -n " - ack"
		count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMPJoinPortAckRx |
			awk '{print $2}'`
		[ -z "$count" ] && count=0
		if [ "$count" != "$ack_nr" ]; then
			echo "[fail] got $count JOIN[s] ack with a different \
				port-number expected $ack_nr"
			ret=1
			dump_stats=1
		else
			echo "[ ok ]"
		fi

		printf "%-39s %s" " " "syn"
		count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMismatchPortSynRx |
			awk '{print $2}'`
		[ -z "$count" ] && count=0
		if [ "$count" != "$mis_syn_nr" ]; then
			echo "[fail] got $count JOIN[s] syn with a mismatched \
				port-number expected $mis_syn_nr"
			ret=1
			dump_stats=1
		else
			echo -n "[ ok ]"
		fi

		echo -n " - ack   "
		count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMismatchPortAckRx |
			awk '{print $2}'`
		[ -z "$count" ] && count=0
		if [ "$count" != "$mis_ack_nr" ]; then
			echo "[fail] got $count JOIN[s] ack with a mismatched \
				port-number expected $mis_ack_nr"
			ret=1
			dump_stats=1
		else
			echo "[ ok ]"
		fi
	else
		echo ""
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
	local invert=${3:-""}
	local count
	local dump_stats
	local addr_ns
	local subflow_ns

	if [ -z $invert ]; then
		addr_ns=$ns1
		subflow_ns=$ns2
	elif [ $invert = "invert" ]; then
		addr_ns=$ns2
		subflow_ns=$ns1
	fi

	printf "%-39s %s" " " "rm "
	count=`ip netns exec $addr_ns nstat -as | grep MPTcpExtRmAddr | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$rm_addr_nr" ]; then
		echo "[fail] got $count RM_ADDR[s] expected $rm_addr_nr"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - sf    "
	count=`ip netns exec $subflow_ns nstat -as | grep MPTcpExtRmSubflow | awk '{print $2}'`
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

chk_prio_nr()
{
	local mp_prio_nr_tx=$1
	local mp_prio_nr_rx=$2
	local count
	local dump_stats

	printf "%-39s %s" " " "ptx"
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMPPrioTx | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$mp_prio_nr_tx" ]; then
		echo "[fail] got $count MP_PRIO[s] TX expected $mp_prio_nr_tx"
		ret=1
		dump_stats=1
	else
		echo -n "[ ok ]"
	fi

	echo -n " - prx   "
	count=`ip netns exec $ns1 nstat -as | grep MPTcpExtMPPrioRx | awk '{print $2}'`
	[ -z "$count" ] && count=0
	if [ "$count" != "$mp_prio_nr_rx" ]; then
		echo "[fail] got $count MP_PRIO[s] RX expected $mp_prio_nr_rx"
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

subflows_tests()
{
	reset
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "no JOIN" "0" "0" "0"

	# subflow limited by client
	reset
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "single subflow, limited by client" 0 0 0

	# subflow limited by server
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

	# single subflow, dev
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow dev ns2eth3
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "single subflow, dev" 1 1 1
}

signal_address_tests()
{
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

	# signal addresses
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.4.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "signal addresses" 3 3 3
	chk_add_nr 3 3

	# signal invalid addresses
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.12.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.14.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "signal invalid addresses" 1 1 1
	chk_add_nr 3 3
}

link_failure_tests()
{
	# accept and use add_addr with additional subflows and link loss
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 3
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 1
	chk_join_nr "multiple flows, signal, link failure" 3 3 3
	chk_add_nr 1 1
}

add_addr_timeout_tests()
{
	# add_addr timeout
	reset_with_add_addr_timeout
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow
	chk_join_nr "signal address, ADD_ADDR timeout" 1 1 1
	chk_add_nr 4 0

	# add_addr timeout IPv6
	reset_with_add_addr_timeout 6
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add dead:beef:2::1 flags signal
	run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
	chk_join_nr "signal address, ADD_ADDR6 timeout" 1 1 1
	chk_add_nr 4 0

	# signal addresses timeout
	reset_with_add_addr_timeout
	ip netns exec $ns1 ./pm_nl_ctl limits 2 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	run_tests $ns1 $ns2 10.0.1.1 0 0 0 least
	chk_join_nr "signal addresses, ADD_ADDR timeout" 2 2 2
	chk_add_nr 8 0

	# signal invalid addresses timeout
	reset_with_add_addr_timeout
	ip netns exec $ns1 ./pm_nl_ctl limits 2 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.12.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	run_tests $ns1 $ns2 10.0.1.1 0 0 0 least
	chk_join_nr "invalid address, ADD_ADDR timeout" 1 1 1
	chk_add_nr 8 0
}

remove_tests()
{
	# single subflow, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 0 -1 slow
	chk_join_nr "remove single subflow" 1 1 1
	chk_rm_nr 1 1

	# multiple subflows, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns2 ./pm_nl_ctl limits 0 2
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 0 -2 slow
	chk_join_nr "remove multiple subflows" 2 2 2
	chk_rm_nr 2 2

	# single address, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 10.0.1.1 0 -1 0 slow
	chk_join_nr "remove single address" 1 1 1
	chk_add_nr 1 1
	chk_rm_nr 1 1 invert

	# subflow and signal, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 2
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 -1 -1 slow
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
	run_tests $ns1 $ns2 10.0.1.1 0 -1 -2 slow
	chk_join_nr "remove subflows and signal" 3 3 3
	chk_add_nr 1 1
	chk_rm_nr 2 2

	# addresses remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal id 250
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.4.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	run_tests $ns1 $ns2 10.0.1.1 0 -3 0 slow
	chk_join_nr "remove addresses" 3 3 3
	chk_add_nr 3 3
	chk_rm_nr 3 3 invert

	# invalid addresses remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.12.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.14.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	run_tests $ns1 $ns2 10.0.1.1 0 -3 0 slow
	chk_join_nr "remove invalid addresses" 1 1 1
	chk_add_nr 3 3
	chk_rm_nr 3 1 invert

	# subflows and signal, flush
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 3
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
	chk_join_nr "flush subflows and signal" 3 3 3
	chk_add_nr 1 1
	chk_rm_nr 2 2

	# subflows flush
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.2.2 flags subflow id 150
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
	chk_join_nr "flush subflows" 3 3 3
	chk_rm_nr 3 3

	# addresses flush
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal id 250
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.4.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
	chk_join_nr "flush addresses" 3 3 3
	chk_add_nr 3 3
	chk_rm_nr 3 3 invert

	# invalid addresses flush
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 3 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.12.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.14.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 3 3
	run_tests $ns1 $ns2 10.0.1.1 0 -8 0 slow
	chk_join_nr "flush invalid addresses" 1 1 1
	chk_add_nr 3 3
	chk_rm_nr 3 1 invert

	# remove id 0 subflow
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 0 -9 slow
	chk_join_nr "remove id 0 subflow" 1 1 1
	chk_rm_nr 1 1

	# remove id 0 address
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 10.0.1.1 0 -9 0 slow
	chk_join_nr "remove id 0 address" 1 1 1
	chk_add_nr 1 1
	chk_rm_nr 1 1 invert
}

add_tests()
{
	# add single subflow
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	run_tests $ns1 $ns2 10.0.1.1 0 0 1 slow
	chk_join_nr "add single subflow" 1 1 1

	# add signal address
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 10.0.1.1 0 1 0 slow
	chk_join_nr "add signal address" 1 1 1
	chk_add_nr 1 1

	# add multiple subflows
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns2 ./pm_nl_ctl limits 0 2
	run_tests $ns1 $ns2 10.0.1.1 0 0 2 slow
	chk_join_nr "add multiple subflows" 2 2 2

	# add multiple subflows IPv6
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns2 ./pm_nl_ctl limits 0 2
	run_tests $ns1 $ns2 dead:beef:1::1 0 0 2 slow
	chk_join_nr "add multiple subflows IPv6" 2 2 2

	# add multiple addresses IPv6
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	run_tests $ns1 $ns2 dead:beef:1::1 0 2 0 slow
	chk_join_nr "add multiple addresses IPv6" 2 2 2
	chk_add_nr 2 2
}

ipv6_tests()
{
	# subflow IPv6
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add dead:beef:3::2 flags subflow
	run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
	chk_join_nr "single subflow IPv6" 1 1 1

	# add_address, unused IPv6
	reset
	ip netns exec $ns1 ./pm_nl_ctl add dead:beef:2::1 flags signal
	run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
	chk_join_nr "unused signal address IPv6" 0 0 0
	chk_add_nr 1 1

	# signal address IPv6
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns1 ./pm_nl_ctl add dead:beef:2::1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 dead:beef:1::1 0 0 0 slow
	chk_join_nr "single address IPv6" 1 1 1
	chk_add_nr 1 1

	# single address IPv6, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns1 ./pm_nl_ctl add dead:beef:2::1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 dead:beef:1::1 0 -1 0 slow
	chk_join_nr "remove single address IPv6" 1 1 1
	chk_add_nr 1 1
	chk_rm_nr 1 1 invert

	# subflow and signal IPv6, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns1 ./pm_nl_ctl add dead:beef:2::1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 2
	ip netns exec $ns2 ./pm_nl_ctl add dead:beef:3::2 flags subflow
	run_tests $ns1 $ns2 dead:beef:1::1 0 -1 -1 slow
	chk_join_nr "remove subflow and signal IPv6" 2 2 2
	chk_add_nr 1 1
	chk_rm_nr 1 1
}

v4mapped_tests()
{
	# subflow IPv4-mapped to IPv4-mapped
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add "::ffff:10.0.3.2" flags subflow
	run_tests $ns1 $ns2 "::ffff:10.0.1.1"
	chk_join_nr "single subflow IPv4-mapped" 1 1 1

	# signal address IPv4-mapped with IPv4-mapped sk
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add "::ffff:10.0.2.1" flags signal
	run_tests $ns1 $ns2 "::ffff:10.0.1.1"
	chk_join_nr "signal address IPv4-mapped" 1 1 1
	chk_add_nr 1 1

	# subflow v4-map-v6
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 "::ffff:10.0.1.1"
	chk_join_nr "single subflow v4-map-v6" 1 1 1

	# signal address v4-map-v6
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	run_tests $ns1 $ns2 "::ffff:10.0.1.1"
	chk_join_nr "signal address v4-map-v6" 1 1 1
	chk_add_nr 1 1

	# subflow v6-map-v4
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add "::ffff:10.0.3.2" flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "single subflow v6-map-v4" 1 1 1

	# signal address v6-map-v4
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add "::ffff:10.0.2.1" flags signal
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "signal address v6-map-v4" 1 1 1
	chk_add_nr 1 1

	# no subflow IPv6 to v4 address
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add dead:beef:2::2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "no JOIN with diff families v4-v6" 0 0 0

	# no subflow IPv6 to v4 address even if v6 has a valid v4 at the end
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add dead:beef:2::10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "no JOIN with diff families v4-v6-2" 0 0 0

	# no subflow IPv4 to v6 address, no need to slow down too then
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 dead:beef:1::1
	chk_join_nr "no JOIN with diff families v6-v4" 0 0 0
}

backup_tests()
{
	# single subflow, backup
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow,backup
	run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow nobackup
	chk_join_nr "single subflow, backup" 1 1 1
	chk_prio_nr 0 1

	# single address, backup
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 10.0.1.1 0 0 0 slow backup
	chk_join_nr "single address, backup" 1 1 1
	chk_add_nr 1 1
	chk_prio_nr 1 0
}

add_addr_ports_tests()
{
	# signal address with port
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "signal address with port" 1 1 1
	chk_add_nr 1 1 1

	# subflow and signal with port
	reset
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns2 ./pm_nl_ctl limits 1 2
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "subflow and signal with port" 2 2 2
	chk_add_nr 1 1 1

	# single address with port, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	run_tests $ns1 $ns2 10.0.1.1 0 -1 0 slow
	chk_join_nr "remove single address with port" 1 1 1
	chk_add_nr 1 1 1
	chk_rm_nr 1 1 invert

	# subflow and signal with port, remove
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	ip netns exec $ns2 ./pm_nl_ctl limits 1 2
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 -1 -1 slow
	chk_join_nr "remove subflow and signal with port" 2 2 2
	chk_add_nr 1 1 1
	chk_rm_nr 1 1

	# subflows and signal with port, flush
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 0 3
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	ip netns exec $ns2 ./pm_nl_ctl limits 1 3
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.4.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1 0 -8 -8 slow
	chk_join_nr "flush subflows and signal with port" 3 3 3
	chk_add_nr 1 1
	chk_rm_nr 2 2

	# multiple addresses with port
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 2 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal port 10100
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "multiple addresses with port" 2 2 2
	chk_add_nr 2 2 2

	# multiple addresses with ports
	reset
	ip netns exec $ns1 ./pm_nl_ctl limits 2 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal port 10100
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.3.1 flags signal port 10101
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "multiple addresses with ports" 2 2 2
	chk_add_nr 2 2 2
}

syncookies_tests()
{
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
}

checksum_tests()
{
	# checksum test 0 0
	reset_with_checksum 0 0
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	run_tests $ns1 $ns2 10.0.1.1
	chk_csum_nr "checksum test 0 0"

	# checksum test 1 1
	reset_with_checksum 1 1
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	run_tests $ns1 $ns2 10.0.1.1
	chk_csum_nr "checksum test 1 1"

	# checksum test 0 1
	reset_with_checksum 0 1
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	run_tests $ns1 $ns2 10.0.1.1
	chk_csum_nr "checksum test 0 1"

	# checksum test 1 0
	reset_with_checksum 1 0
	ip netns exec $ns1 ./pm_nl_ctl limits 0 1
	ip netns exec $ns2 ./pm_nl_ctl limits 0 1
	run_tests $ns1 $ns2 10.0.1.1
	chk_csum_nr "checksum test 1 0"
}

deny_join_id0_tests()
{
	# subflow allow join id0 ns1
	reset_with_allow_join_id0 1 0
	ip netns exec $ns1 ./pm_nl_ctl limits 1 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "single subflow allow join id0 ns1" 1 1 1

	# subflow allow join id0 ns2
	reset_with_allow_join_id0 0 1
	ip netns exec $ns1 ./pm_nl_ctl limits 1 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "single subflow allow join id0 ns2" 0 0 0

	# signal address allow join id0 ns1
	# ADD_ADDRs are not affected by allow_join_id0 value.
	reset_with_allow_join_id0 1 0
	ip netns exec $ns1 ./pm_nl_ctl limits 1 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "signal address allow join id0 ns1" 1 1 1
	chk_add_nr 1 1

	# signal address allow join id0 ns2
	# ADD_ADDRs are not affected by allow_join_id0 value.
	reset_with_allow_join_id0 0 1
	ip netns exec $ns1 ./pm_nl_ctl limits 1 1
	ip netns exec $ns2 ./pm_nl_ctl limits 1 1
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "signal address allow join id0 ns2" 1 1 1
	chk_add_nr 1 1

	# subflow and address allow join id0 ns1
	reset_with_allow_join_id0 1 0
	ip netns exec $ns1 ./pm_nl_ctl limits 2 2
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "subflow and address allow join id0 1" 2 2 2

	# subflow and address allow join id0 ns2
	reset_with_allow_join_id0 0 1
	ip netns exec $ns1 ./pm_nl_ctl limits 2 2
	ip netns exec $ns2 ./pm_nl_ctl limits 2 2
	ip netns exec $ns1 ./pm_nl_ctl add 10.0.2.1 flags signal
	ip netns exec $ns2 ./pm_nl_ctl add 10.0.3.2 flags subflow
	run_tests $ns1 $ns2 10.0.1.1
	chk_join_nr "subflow and address allow join id0 2" 1 1 1
}

all_tests()
{
	subflows_tests
	signal_address_tests
	link_failure_tests
	add_addr_timeout_tests
	remove_tests
	add_tests
	ipv6_tests
	v4mapped_tests
	backup_tests
	add_addr_ports_tests
	syncookies_tests
	checksum_tests
	deny_join_id0_tests
}

usage()
{
	echo "mptcp_join usage:"
	echo "  -f subflows_tests"
	echo "  -s signal_address_tests"
	echo "  -l link_failure_tests"
	echo "  -t add_addr_timeout_tests"
	echo "  -r remove_tests"
	echo "  -a add_tests"
	echo "  -6 ipv6_tests"
	echo "  -4 v4mapped_tests"
	echo "  -b backup_tests"
	echo "  -p add_addr_ports_tests"
	echo "  -k syncookies_tests"
	echo "  -S checksum_tests"
	echo "  -d deny_join_id0_tests"
	echo "  -c capture pcap files"
	echo "  -C enable data checksum"
	echo "  -h help"
}

sin=$(mktemp)
sout=$(mktemp)
cin=$(mktemp)
cinsent=$(mktemp)
cout=$(mktemp)
init
make_file "$cin" "client" 1
make_file "$sin" "server" 1
trap cleanup EXIT

for arg in "$@"; do
	# check for "capture/checksum" args before launching tests
	if [[ "${arg}" =~ ^"-"[0-9a-zA-Z]*"c"[0-9a-zA-Z]*$ ]]; then
		capture=1
	fi
	if [[ "${arg}" =~ ^"-"[0-9a-zA-Z]*"C"[0-9a-zA-Z]*$ ]]; then
		checksum=1
	fi

	# exception for the capture/checksum options, the rest means: a part of the tests
	if [ "${arg}" != "-c" ] && [ "${arg}" != "-C" ]; then
		do_all_tests=0
	fi
done

if [ $do_all_tests -eq 1 ]; then
	all_tests
	exit $ret
fi

while getopts 'fsltra64bpkdchCS' opt; do
	case $opt in
		f)
			subflows_tests
			;;
		s)
			signal_address_tests
			;;
		l)
			link_failure_tests
			;;
		t)
			add_addr_timeout_tests
			;;
		r)
			remove_tests
			;;
		a)
			add_tests
			;;
		6)
			ipv6_tests
			;;
		4)
			v4mapped_tests
			;;
		b)
			backup_tests
			;;
		p)
			add_addr_ports_tests
			;;
		k)
			syncookies_tests
			;;
		S)
			checksum_tests
			;;
		d)
			deny_join_id0_tests
			;;
		c)
			;;
		C)
			;;
		h | *)
			usage
			;;
	esac
done

exit $ret
