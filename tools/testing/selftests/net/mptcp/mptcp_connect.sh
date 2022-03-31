#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

time_start=$(date +%s)

optstring="S:R:d:e:l:r:h4cm:f:tC"
ret=0
sin=""
sout=""
cin_disconnect=""
cin=""
cout=""
ksft_skip=4
capture=false
timeout_poll=30
timeout_test=$((timeout_poll * 2 + 1))
ipv6=true
ethtool_random_on=true
tc_delay="$((RANDOM%50))"
tc_loss=$((RANDOM%101))
testmode=""
sndbuf=0
rcvbuf=0
options_log=true
do_tcp=0
checksum=false
filesize=0
connect_per_transfer=1

if [ $tc_loss -eq 100 ];then
	tc_loss=1%
elif [ $tc_loss -ge 10 ]; then
	tc_loss=0.$tc_loss%
elif [ $tc_loss -ge 1 ]; then
	tc_loss=0.0$tc_loss%
else
	tc_loss=""
fi

usage() {
	echo "Usage: $0 [ -a ]"
	echo -e "\t-d: tc/netem delay in milliseconds, e.g. \"-d 10\" (default random)"
	echo -e "\t-l: tc/netem loss percentage, e.g. \"-l 0.02\" (default random)"
	echo -e "\t-r: tc/netem reorder mode, e.g. \"-r 25% 50% gap 5\", use "-r 0" to disable reordering (default random)"
	echo -e "\t-e: ethtool features to disable, e.g.: \"-e tso -e gso\" (default: randomly disable any of tso/gso/gro)"
	echo -e "\t-4: IPv4 only: disable IPv6 tests (default: test both IPv4 and IPv6)"
	echo -e "\t-c: capture packets for each test using tcpdump (default: no capture)"
	echo -e "\t-f: size of file to transfer in bytes (default random)"
	echo -e "\t-S: set sndbuf value (default: use kernel default)"
	echo -e "\t-R: set rcvbuf value (default: use kernel default)"
	echo -e "\t-m: test mode (poll, sendfile; default: poll)"
	echo -e "\t-t: also run tests with TCP (use twice to non-fallback tcp)"
	echo -e "\t-C: enable the MPTCP data checksum"
}

while getopts "$optstring" option;do
	case "$option" in
	"h")
		usage $0
		exit 0
		;;
	"d")
		if [ $OPTARG -ge 0 ];then
			tc_delay="$OPTARG"
		else
			echo "-d requires numeric argument, got \"$OPTARG\"" 1>&2
			exit 1
		fi
		;;
	"e")
		ethtool_args="$ethtool_args $OPTARG off"
		ethtool_random_on=false
		;;
	"l")
		tc_loss="$OPTARG"
		;;
	"r")
		tc_reorder="$OPTARG"
		;;
	"4")
		ipv6=false
		;;
	"c")
		capture=true
		;;
	"S")
		if [ $OPTARG -ge 0 ];then
			sndbuf="$OPTARG"
		else
			echo "-S requires numeric argument, got \"$OPTARG\"" 1>&2
			exit 1
		fi
		;;
	"R")
		if [ $OPTARG -ge 0 ];then
			rcvbuf="$OPTARG"
		else
			echo "-R requires numeric argument, got \"$OPTARG\"" 1>&2
			exit 1
		fi
		;;
	"m")
		testmode="$OPTARG"
		;;
	"f")
		filesize="$OPTARG"
		;;
	"t")
		do_tcp=$((do_tcp+1))
		;;
	"C")
		checksum=true
		;;
	"?")
		usage $0
		exit 1
		;;
	esac
done

sec=$(date +%s)
rndh=$(printf %x $sec)-$(mktemp -u XXXXXX)
ns1="ns1-$rndh"
ns2="ns2-$rndh"
ns3="ns3-$rndh"
ns4="ns4-$rndh"

TEST_COUNT=0

cleanup()
{
	rm -f "$cin_disconnect" "$cout_disconnect"
	rm -f "$cin" "$cout"
	rm -f "$sin" "$sout"
	rm -f "$capout"

	local netns
	for netns in "$ns1" "$ns2" "$ns3" "$ns4";do
		ip netns del $netns
		rm -f /tmp/$netns.{nstat,out}
	done
}

ip -Version > /dev/null 2>&1
if [ $? -ne 0 ];then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

sin=$(mktemp)
sout=$(mktemp)
cin=$(mktemp)
cout=$(mktemp)
capout=$(mktemp)
cin_disconnect="$cin".disconnect
cout_disconnect="$cout".disconnect
trap cleanup EXIT

for i in "$ns1" "$ns2" "$ns3" "$ns4";do
	ip netns add $i || exit $ksft_skip
	ip -net $i link set lo up
done

#  "$ns1"              ns2                    ns3                     ns4
# ns1eth2    ns2eth1   ns2eth3      ns3eth2   ns3eth4       ns4eth3
#                           - drop 1% ->            reorder 25%
#                           <- TSO off -

ip link add ns1eth2 netns "$ns1" type veth peer name ns2eth1 netns "$ns2"
ip link add ns2eth3 netns "$ns2" type veth peer name ns3eth2 netns "$ns3"
ip link add ns3eth4 netns "$ns3" type veth peer name ns4eth3 netns "$ns4"

ip -net "$ns1" addr add 10.0.1.1/24 dev ns1eth2
ip -net "$ns1" addr add dead:beef:1::1/64 dev ns1eth2 nodad

ip -net "$ns1" link set ns1eth2 up
ip -net "$ns1" route add default via 10.0.1.2
ip -net "$ns1" route add default via dead:beef:1::2

ip -net "$ns2" addr add 10.0.1.2/24 dev ns2eth1
ip -net "$ns2" addr add dead:beef:1::2/64 dev ns2eth1 nodad
ip -net "$ns2" link set ns2eth1 up

ip -net "$ns2" addr add 10.0.2.1/24 dev ns2eth3
ip -net "$ns2" addr add dead:beef:2::1/64 dev ns2eth3 nodad
ip -net "$ns2" link set ns2eth3 up
ip -net "$ns2" route add default via 10.0.2.2
ip -net "$ns2" route add default via dead:beef:2::2
ip netns exec "$ns2" sysctl -q net.ipv4.ip_forward=1
ip netns exec "$ns2" sysctl -q net.ipv6.conf.all.forwarding=1

ip -net "$ns3" addr add 10.0.2.2/24 dev ns3eth2
ip -net "$ns3" addr add dead:beef:2::2/64 dev ns3eth2 nodad
ip -net "$ns3" link set ns3eth2 up

ip -net "$ns3" addr add 10.0.3.2/24 dev ns3eth4
ip -net "$ns3" addr add dead:beef:3::2/64 dev ns3eth4 nodad
ip -net "$ns3" link set ns3eth4 up
ip -net "$ns3" route add default via 10.0.2.1
ip -net "$ns3" route add default via dead:beef:2::1
ip netns exec "$ns3" sysctl -q net.ipv4.ip_forward=1
ip netns exec "$ns3" sysctl -q net.ipv6.conf.all.forwarding=1

ip -net "$ns4" addr add 10.0.3.1/24 dev ns4eth3
ip -net "$ns4" addr add dead:beef:3::1/64 dev ns4eth3 nodad
ip -net "$ns4" link set ns4eth3 up
ip -net "$ns4" route add default via 10.0.3.2
ip -net "$ns4" route add default via dead:beef:3::2

if $checksum; then
	for i in "$ns1" "$ns2" "$ns3" "$ns4";do
		ip netns exec $i sysctl -q net.mptcp.checksum_enabled=1
	done
fi

set_ethtool_flags() {
	local ns="$1"
	local dev="$2"
	local flags="$3"

	ip netns exec $ns ethtool -K $dev $flags 2>/dev/null
	[ $? -eq 0 ] && echo "INFO: set $ns dev $dev: ethtool -K $flags"
}

set_random_ethtool_flags() {
	local flags=""
	local r=$RANDOM

	local pick1=$((r & 1))
	local pick2=$((r & 2))
	local pick3=$((r & 4))

	[ $pick1 -ne 0 ] && flags="tso off"
	[ $pick2 -ne 0 ] && flags="$flags gso off"
	[ $pick3 -ne 0 ] && flags="$flags gro off"

	[ -z "$flags" ] && return

	set_ethtool_flags "$1" "$2" "$flags"
}

if $ethtool_random_on;then
	set_random_ethtool_flags "$ns3" ns3eth2
	set_random_ethtool_flags "$ns4" ns4eth3
else
	set_ethtool_flags "$ns3" ns3eth2 "$ethtool_args"
	set_ethtool_flags "$ns4" ns4eth3 "$ethtool_args"
fi

print_file_err()
{
	ls -l "$1" 1>&2
	echo "Trailing bytes are: "
	tail -c 27 "$1"
}

check_transfer()
{
	local in=$1
	local out=$2
	local what=$3

	cmp "$in" "$out" > /dev/null 2>&1
	if [ $? -ne 0 ] ;then
		echo "[ FAIL ] $what does not match (in, out):"
		print_file_err "$in"
		print_file_err "$out"

		return 1
	fi

	return 0
}

check_mptcp_disabled()
{
	local disabled_ns
	disabled_ns="ns_disabled-$sech-$(mktemp -u XXXXXX)"
	ip netns add ${disabled_ns} || exit $ksft_skip

	# net.mptcp.enabled should be enabled by default
	if [ "$(ip netns exec ${disabled_ns} sysctl net.mptcp.enabled | awk '{ print $3 }')" -ne 1 ]; then
		echo -e "net.mptcp.enabled sysctl is not 1 by default\t\t[ FAIL ]"
		ret=1
		return 1
	fi
	ip netns exec ${disabled_ns} sysctl -q net.mptcp.enabled=0

	local err=0
	LC_ALL=C ip netns exec ${disabled_ns} ./mptcp_connect -p 10000 -s MPTCP 127.0.0.1 < "$cin" 2>&1 | \
		grep -q "^socket: Protocol not available$" && err=1
	ip netns delete ${disabled_ns}

	if [ ${err} -eq 0 ]; then
		echo -e "New MPTCP socket cannot be blocked via sysctl\t\t[ FAIL ]"
		ret=1
		return 1
	fi

	echo -e "New MPTCP socket can be blocked via sysctl\t\t[ OK ]"
	return 0
}

# $1: IP address
is_v6()
{
	[ -z "${1##*:*}" ]
}

do_ping()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local connect_addr="$3"
	local ping_args="-q -c 1"

	if is_v6 "${connect_addr}"; then
		$ipv6 || return 0
		ping_args="${ping_args} -6"
	fi

	ip netns exec ${connector_ns} ping ${ping_args} $connect_addr >/dev/null
	if [ $? -ne 0 ] ; then
		echo "$listener_ns -> $connect_addr connectivity [ FAIL ]" 1>&2
		ret=1

		return 1
	fi

	return 0
}

# $1: ns, $2: MIB counter
get_mib_counter()
{
	local listener_ns="${1}"
	local mib="${2}"

	# strip the header
	ip netns exec "${listener_ns}" \
		nstat -z -a "${mib}" | \
			tail -n+2 | \
			while read a count c rest; do
				echo $count
			done
}

# $1: ns, $2: port
wait_local_port_listen()
{
	local listener_ns="${1}"
	local port="${2}"

	local port_hex i

	port_hex="$(printf "%04X" "${port}")"
	for i in $(seq 10); do
		ip netns exec "${listener_ns}" cat /proc/net/tcp* | \
			awk "BEGIN {rc=1} {if (\$2 ~ /:${port_hex}\$/ && \$4 ~ /0A/) {rc=0; exit}} END {exit rc}" &&
			break
		sleep 0.1
	done
}

do_transfer()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local cl_proto="$3"
	local srv_proto="$4"
	local connect_addr="$5"
	local local_addr="$6"
	local extra_args="$7"

	local port
	port=$((10000+$TEST_COUNT))
	TEST_COUNT=$((TEST_COUNT+1))

	if [ "$rcvbuf" -gt 0 ]; then
		extra_args="$extra_args -R $rcvbuf"
	fi

	if [ "$sndbuf" -gt 0 ]; then
		extra_args="$extra_args -S $sndbuf"
	fi

	if [ -n "$testmode" ]; then
		extra_args="$extra_args -m $testmode"
	fi

	if [ -n "$extra_args" ] && $options_log; then
		echo "INFO: extra options: $extra_args"
	fi
	options_log=false

	:> "$cout"
	:> "$sout"
	:> "$capout"

	local addr_port
	addr_port=$(printf "%s:%d" ${connect_addr} ${port})
	printf "%.3s %-5s -> %.3s (%-20s) %-5s\t" ${connector_ns} ${cl_proto} ${listener_ns} ${addr_port} ${srv_proto}

	if $capture; then
		local capuser
		if [ -z $SUDO_USER ] ; then
			capuser=""
		else
			capuser="-Z $SUDO_USER"
		fi

		local capfile="${rndh}-${connector_ns:0:3}-${listener_ns:0:3}-${cl_proto}-${srv_proto}-${connect_addr}-${port}"
		local capopt="-i any -s 65535 -B 32768 ${capuser}"

		ip netns exec ${listener_ns}  tcpdump ${capopt} -w "${capfile}-listener.pcap"  >> "${capout}" 2>&1 &
		local cappid_listener=$!

		ip netns exec ${connector_ns} tcpdump ${capopt} -w "${capfile}-connector.pcap" >> "${capout}" 2>&1 &
		local cappid_connector=$!

		sleep 1
	fi

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat -n
	if [ ${listener_ns} != ${connector_ns} ]; then
		NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
			nstat -n
	fi

	local stat_synrx_last_l=$(get_mib_counter "${listener_ns}" "MPTcpExtMPCapableSYNRX")
	local stat_ackrx_last_l=$(get_mib_counter "${listener_ns}" "MPTcpExtMPCapableACKRX")
	local stat_cookietx_last=$(get_mib_counter "${listener_ns}" "TcpExtSyncookiesSent")
	local stat_cookierx_last=$(get_mib_counter "${listener_ns}" "TcpExtSyncookiesRecv")

	timeout ${timeout_test} \
		ip netns exec ${listener_ns} \
			./mptcp_connect -t ${timeout_poll} -l -p $port -s ${srv_proto} \
				$extra_args $local_addr < "$sin" > "$sout" &
	local spid=$!

	wait_local_port_listen "${listener_ns}" "${port}"

	local start
	start=$(date +%s%3N)
	timeout ${timeout_test} \
		ip netns exec ${connector_ns} \
			./mptcp_connect -t ${timeout_poll} -p $port -s ${cl_proto} \
				$extra_args $connect_addr < "$cin" > "$cout" &
	local cpid=$!

	wait $cpid
	local retc=$?
	wait $spid
	local rets=$?

	local stop
	stop=$(date +%s%3N)

	if $capture; then
		sleep 1
		kill ${cappid_listener}
		kill ${cappid_connector}
	fi

	NSTAT_HISTORY=/tmp/${listener_ns}.nstat ip netns exec ${listener_ns} \
		nstat | grep Tcp > /tmp/${listener_ns}.out
	if [ ${listener_ns} != ${connector_ns} ]; then
		NSTAT_HISTORY=/tmp/${connector_ns}.nstat ip netns exec ${connector_ns} \
			nstat | grep Tcp > /tmp/${connector_ns}.out
	fi

	local duration
	duration=$((stop-start))
	printf "(duration %05sms) " "${duration}"
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		echo "[ FAIL ] client exit code $retc, server $rets" 1>&2
		echo -e "\nnetns ${listener_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${listener_ns} ss -Menita 1>&2 -o "sport = :$port"
		cat /tmp/${listener_ns}.out
		echo -e "\nnetns ${connector_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${connector_ns} ss -Menita 1>&2 -o "dport = :$port"
		[ ${listener_ns} != ${connector_ns} ] && cat /tmp/${connector_ns}.out

		echo
		cat "$capout"
		return 1
	fi

	check_transfer $sin $cout "file received by client"
	retc=$?
	check_transfer $cin $sout "file received by server"
	rets=$?

	local stat_synrx_now_l=$(get_mib_counter "${listener_ns}" "MPTcpExtMPCapableSYNRX")
	local stat_ackrx_now_l=$(get_mib_counter "${listener_ns}" "MPTcpExtMPCapableACKRX")
	local stat_cookietx_now=$(get_mib_counter "${listener_ns}" "TcpExtSyncookiesSent")
	local stat_cookierx_now=$(get_mib_counter "${listener_ns}" "TcpExtSyncookiesRecv")
	local stat_ooo_now=$(get_mib_counter "${listener_ns}" "TcpExtTCPOFOQueue")

	expect_synrx=$((stat_synrx_last_l))
	expect_ackrx=$((stat_ackrx_last_l))

	cookies=$(ip netns exec ${listener_ns} sysctl net.ipv4.tcp_syncookies)
	cookies=${cookies##*=}

	if [ ${cl_proto} = "MPTCP" ] && [ ${srv_proto} = "MPTCP" ]; then
		expect_synrx=$((stat_synrx_last_l+$connect_per_transfer))
		expect_ackrx=$((stat_ackrx_last_l+$connect_per_transfer))
	fi

	if [ ${stat_synrx_now_l} -lt ${expect_synrx} ]; then
		printf "[ FAIL ] lower MPC SYN rx (%d) than expected (%d)\n" \
			"${stat_synrx_now_l}" "${expect_synrx}" 1>&2
		retc=1
	fi
	if [ ${stat_ackrx_now_l} -lt ${expect_ackrx} -a ${stat_ooo_now} -eq 0 ]; then
		if [ ${stat_ooo_now} -eq 0 ]; then
			printf "[ FAIL ] lower MPC ACK rx (%d) than expected (%d)\n" \
				"${stat_ackrx_now_l}" "${expect_ackrx}" 1>&2
			rets=1
		else
			printf "[ Note ] fallback due to TCP OoO"
		fi
	fi

	if [ $retc -eq 0 ] && [ $rets -eq 0 ]; then
		printf "[ OK ]"
	fi

	if [ $cookies -eq 2 ];then
		if [ $stat_cookietx_last -ge $stat_cookietx_now ] ;then
			printf " WARN: CookieSent: did not advance"
		fi
		if [ $stat_cookierx_last -ge $stat_cookierx_now ] ;then
			printf " WARN: CookieRecv: did not advance"
		fi
	else
		if [ $stat_cookietx_last -ne $stat_cookietx_now ] ;then
			printf " WARN: CookieSent: changed"
		fi
		if [ $stat_cookierx_last -ne $stat_cookierx_now ] ;then
			printf " WARN: CookieRecv: changed"
		fi
	fi

	if [ ${stat_synrx_now_l} -gt ${expect_synrx} ]; then
		printf " WARN: SYNRX: expect %d, got %d (probably retransmissions)" \
			"${expect_synrx}" "${stat_synrx_now_l}"
	fi
	if [ ${stat_ackrx_now_l} -gt ${expect_ackrx} ]; then
		printf " WARN: ACKRX: expect %d, got %d (probably retransmissions)" \
			"${expect_ackrx}" "${stat_ackrx_now_l}"
	fi

	echo
	cat "$capout"
	[ $retc -eq 0 ] && [ $rets -eq 0 ]
}

make_file()
{
	local name=$1
	local who=$2
	local SIZE=$filesize
	local ksize
	local rem

	if [ $SIZE -eq 0 ]; then
		local MAXSIZE=$((1024 * 1024 * 8))
		local MINSIZE=$((1024 * 256))

		SIZE=$(((RANDOM * RANDOM + MINSIZE) % MAXSIZE))
	fi

	ksize=$((SIZE / 1024))
	rem=$((SIZE - (ksize * 1024)))

	dd if=/dev/urandom of="$name" bs=1024 count=$ksize 2> /dev/null
	dd if=/dev/urandom conv=notrunc of="$name" bs=1 count=$rem 2> /dev/null
	echo -e "\nMPTCP_TEST_FILE_END_MARKER" >> "$name"

	echo "Created $name (size $(du -b "$name")) containing data sent by $who"
}

run_tests_lo()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local connect_addr="$3"
	local loopback="$4"
	local extra_args="$5"
	local lret=0

	# skip if test programs are running inside same netns for subsequent runs.
	if [ $loopback -eq 0 ] && [ ${listener_ns} = ${connector_ns} ]; then
		return 0
	fi

	# skip if we don't want v6
	if ! $ipv6 && is_v6 "${connect_addr}"; then
		return 0
	fi

	local local_addr
	if is_v6 "${connect_addr}"; then
		local_addr="::"
	else
		local_addr="0.0.0.0"
	fi

	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP \
		    ${connect_addr} ${local_addr} "${extra_args}"
	lret=$?
	if [ $lret -ne 0 ]; then
		ret=$lret
		return 1
	fi

	if [ $do_tcp -eq 0 ]; then
		# don't bother testing fallback tcp except for loopback case.
		if [ ${listener_ns} != ${connector_ns} ]; then
			return 0
		fi
	fi

	do_transfer ${listener_ns} ${connector_ns} MPTCP TCP \
		    ${connect_addr} ${local_addr} "${extra_args}"
	lret=$?
	if [ $lret -ne 0 ]; then
		ret=$lret
		return 1
	fi

	do_transfer ${listener_ns} ${connector_ns} TCP MPTCP \
		    ${connect_addr} ${local_addr} "${extra_args}"
	lret=$?
	if [ $lret -ne 0 ]; then
		ret=$lret
		return 1
	fi

	if [ $do_tcp -gt 1 ] ;then
		do_transfer ${listener_ns} ${connector_ns} TCP TCP \
			    ${connect_addr} ${local_addr} "${extra_args}"
		lret=$?
		if [ $lret -ne 0 ]; then
			ret=$lret
			return 1
		fi
	fi

	return 0
}

run_tests()
{
	run_tests_lo $1 $2 $3 0
}

run_test_transparent()
{
	local connect_addr="$1"
	local msg="$2"

	local connector_ns="$ns1"
	local listener_ns="$ns2"
	local lret=0
	local r6flag=""

	# skip if we don't want v6
	if ! $ipv6 && is_v6 "${connect_addr}"; then
		return 0
	fi

ip netns exec "$listener_ns" nft -f /dev/stdin <<"EOF"
flush ruleset
table inet mangle {
	chain divert {
		type filter hook prerouting priority -150;

		meta l4proto tcp socket transparent 1 meta mark set 1 accept
		tcp dport 20000 tproxy to :20000 meta mark set 1 accept
	}
}
EOF
	if [ $? -ne 0 ]; then
		echo "SKIP: $msg, could not load nft ruleset"
		return
	fi

	local local_addr
	if is_v6 "${connect_addr}"; then
		local_addr="::"
		r6flag="-6"
	else
		local_addr="0.0.0.0"
	fi

	ip -net "$listener_ns" $r6flag rule add fwmark 1 lookup 100
	if [ $? -ne 0 ]; then
		ip netns exec "$listener_ns" nft flush ruleset
		echo "SKIP: $msg, ip $r6flag rule failed"
		return
	fi

	ip -net "$listener_ns" route add local $local_addr/0 dev lo table 100
	if [ $? -ne 0 ]; then
		ip netns exec "$listener_ns" nft flush ruleset
		ip -net "$listener_ns" $r6flag rule del fwmark 1 lookup 100
		echo "SKIP: $msg, ip route add local $local_addr failed"
		return
	fi

	echo "INFO: test $msg"

	TEST_COUNT=10000
	local extra_args="-o TRANSPARENT"
	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP \
		    ${connect_addr} ${local_addr} "${extra_args}"
	lret=$?

	ip netns exec "$listener_ns" nft flush ruleset
	ip -net "$listener_ns" $r6flag rule del fwmark 1 lookup 100
	ip -net "$listener_ns" route del local $local_addr/0 dev lo table 100

	if [ $lret -ne 0 ]; then
		echo "FAIL: $msg, mptcp connection error" 1>&2
		ret=$lret
		return 1
	fi

	echo "PASS: $msg"
	return 0
}

run_tests_peekmode()
{
	local peekmode="$1"

	echo "INFO: with peek mode: ${peekmode}"
	run_tests_lo "$ns1" "$ns1" 10.0.1.1 1 "-P ${peekmode}"
	run_tests_lo "$ns1" "$ns1" dead:beef:1::1 1 "-P ${peekmode}"
}

run_tests_disconnect()
{
	local peekmode="$1"
	local old_cin=$cin
	local old_sin=$sin

	cat $cin $cin $cin > "$cin".disconnect

	# force do_transfer to cope with the multiple tranmissions
	sin="$cin.disconnect"
	sin_disconnect=$old_sin
	cin="$cin.disconnect"
	cin_disconnect="$old_cin"
	connect_per_transfer=3

	echo "INFO: disconnect"
	run_tests_lo "$ns1" "$ns1" 10.0.1.1 1 "-I 3 -i $old_cin"
	run_tests_lo "$ns1" "$ns1" dead:beef:1::1 1 "-I 3 -i $old_cin"

	# restore previous status
	sin=$old_sin
	sin_disconnect="$cout".disconnect
	cin=$old_cin
	cin_disconnect="$cin".disconnect
	connect_per_transfer=1
}

display_time()
{
	time_end=$(date +%s)
	time_run=$((time_end-time_start))

	echo "Time: ${time_run} seconds"
}

stop_if_error()
{
	local msg="$1"

	if [ ${ret} -ne 0 ]; then
		echo "FAIL: ${msg}" 1>&2
		display_time
		exit ${ret}
	fi
}

make_file "$cin" "client"
make_file "$sin" "server"

check_mptcp_disabled

stop_if_error "The kernel configuration is not valid for MPTCP"

echo "INFO: validating network environment with pings"
for sender in "$ns1" "$ns2" "$ns3" "$ns4";do
	do_ping "$ns1" $sender 10.0.1.1
	do_ping "$ns1" $sender dead:beef:1::1

	do_ping "$ns2" $sender 10.0.1.2
	do_ping "$ns2" $sender dead:beef:1::2
	do_ping "$ns2" $sender 10.0.2.1
	do_ping "$ns2" $sender dead:beef:2::1

	do_ping "$ns3" $sender 10.0.2.2
	do_ping "$ns3" $sender dead:beef:2::2
	do_ping "$ns3" $sender 10.0.3.2
	do_ping "$ns3" $sender dead:beef:3::2

	do_ping "$ns4" $sender 10.0.3.1
	do_ping "$ns4" $sender dead:beef:3::1
done

stop_if_error "Could not even run ping tests"

[ -n "$tc_loss" ] && tc -net "$ns2" qdisc add dev ns2eth3 root netem loss random $tc_loss delay ${tc_delay}ms
echo -n "INFO: Using loss of $tc_loss "
test "$tc_delay" -gt 0 && echo -n "delay $tc_delay ms "

reorder_delay=$(($tc_delay / 4))

if [ -z "${tc_reorder}" ]; then
	reorder1=$((RANDOM%10))
	reorder1=$((100 - reorder1))
	reorder2=$((RANDOM%100))

	if [ $reorder_delay -gt 0 ] && [ $reorder1 -lt 100 ] && [ $reorder2 -gt 0 ]; then
		tc_reorder="reorder ${reorder1}% ${reorder2}%"
		echo -n "$tc_reorder with delay ${reorder_delay}ms "
	fi
elif [ "$tc_reorder" = "0" ];then
	tc_reorder=""
elif [ "$reorder_delay" -gt 0 ];then
	# reordering requires some delay
	tc_reorder="reorder $tc_reorder"
	echo -n "$tc_reorder with delay ${reorder_delay}ms "
fi

echo "on ns3eth4"

tc -net "$ns3" qdisc add dev ns3eth4 root netem delay ${reorder_delay}ms $tc_reorder

run_tests_lo "$ns1" "$ns1" 10.0.1.1 1
stop_if_error "Could not even run loopback test"

run_tests_lo "$ns1" "$ns1" dead:beef:1::1 1
stop_if_error "Could not even run loopback v6 test"

for sender in $ns1 $ns2 $ns3 $ns4;do
	# ns1<->ns2 is not subject to reordering/tc delays. Use it to test
	# mptcp syncookie support.
	if [ $sender = $ns1 ]; then
		ip netns exec "$ns2" sysctl -q net.ipv4.tcp_syncookies=2
	else
		ip netns exec "$ns2" sysctl -q net.ipv4.tcp_syncookies=1
	fi

	run_tests "$ns1" $sender 10.0.1.1
	run_tests "$ns1" $sender dead:beef:1::1

	run_tests "$ns2" $sender 10.0.1.2
	run_tests "$ns2" $sender dead:beef:1::2
	run_tests "$ns2" $sender 10.0.2.1
	run_tests "$ns2" $sender dead:beef:2::1

	run_tests "$ns3" $sender 10.0.2.2
	run_tests "$ns3" $sender dead:beef:2::2
	run_tests "$ns3" $sender 10.0.3.2
	run_tests "$ns3" $sender dead:beef:3::2

	run_tests "$ns4" $sender 10.0.3.1
	run_tests "$ns4" $sender dead:beef:3::1

	stop_if_error "Tests with $sender as a sender have failed"
done

run_tests_peekmode "saveWithPeek"
run_tests_peekmode "saveAfterPeek"
stop_if_error "Tests with peek mode have failed"

# connect to ns4 ip address, ns2 should intercept/proxy
run_test_transparent 10.0.3.1 "tproxy ipv4"
run_test_transparent dead:beef:3::1 "tproxy ipv6"
stop_if_error "Tests with tproxy have failed"

run_tests_disconnect

display_time
exit $ret
