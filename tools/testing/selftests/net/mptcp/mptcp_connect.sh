#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it, especially because there were too many before having
# address all other issues detected by shellcheck.
#shellcheck disable=SC2086

. "$(dirname "${0}")/mptcp_lib.sh"

time_start=$(date +%s)

optstring="S:R:d:e:l:r:h4cm:f:tC"
ret=0
final_ret=0
sin=""
sout=""
cin_disconnect=""
cin=""
cout=""
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
port=$((10000 - 1))

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
		exit ${KSFT_PASS}
		;;
	"d")
		if [ $OPTARG -ge 0 ];then
			tc_delay="$OPTARG"
		else
			echo "-d requires numeric argument, got \"$OPTARG\"" 1>&2
			exit ${KSFT_FAIL}
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
			exit ${KSFT_FAIL}
		fi
		;;
	"R")
		if [ $OPTARG -ge 0 ];then
			rcvbuf="$OPTARG"
		else
			echo "-R requires numeric argument, got \"$OPTARG\"" 1>&2
			exit ${KSFT_FAIL}
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
		exit ${KSFT_FAIL}
		;;
	esac
done

ns1=""
ns2=""
ns3=""
ns4=""

TEST_GROUP=""

# This function is used in the cleanup trap
#shellcheck disable=SC2317
cleanup()
{
	rm -f "$cin_disconnect" "$cout_disconnect"
	rm -f "$cin" "$cout"
	rm -f "$sin" "$sout"
	rm -f "$capout"

	mptcp_lib_ns_exit "${ns1}" "${ns2}" "${ns3}" "${ns4}"
}

mptcp_lib_check_mptcp
mptcp_lib_check_kallsyms
mptcp_lib_check_tools ip

sin=$(mktemp)
sout=$(mktemp)
cin=$(mktemp)
cout=$(mktemp)
capout=$(mktemp)
cin_disconnect="$cin".disconnect
cout_disconnect="$cout".disconnect
trap cleanup EXIT

mptcp_lib_ns_init ns1 ns2 ns3 ns4

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

	if ip netns exec $ns ethtool -K $dev $flags 2>/dev/null; then
		mptcp_lib_pr_info "set $ns dev $dev: ethtool -K $flags"
	fi
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

print_larger_title() {
	# here we don't have the time, a bit longer for the alignment
	MPTCP_LIB_TEST_FORMAT="%02u %-69s" \
		mptcp_lib_print_title "${@}"
}

check_mptcp_disabled()
{
	local disabled_ns
	mptcp_lib_ns_init disabled_ns

	print_larger_title "New MPTCP socket can be blocked via sysctl"
	# net.mptcp.enabled should be enabled by default
	if [ "$(ip netns exec ${disabled_ns} sysctl net.mptcp.enabled | awk '{ print $3 }')" -ne 1 ]; then
		mptcp_lib_pr_fail "net.mptcp.enabled sysctl is not 1 by default"
		mptcp_lib_result_fail "net.mptcp.enabled sysctl is not 1 by default"
		ret=${KSFT_FAIL}
		return 1
	fi
	ip netns exec ${disabled_ns} sysctl -q net.mptcp.enabled=0

	local err=0
	LC_ALL=C ip netns exec ${disabled_ns} ./mptcp_connect -p 10000 -s MPTCP 127.0.0.1 < "$cin" 2>&1 | \
		grep -q "^socket: Protocol not available$" && err=1
	mptcp_lib_ns_exit "${disabled_ns}"

	if [ ${err} -eq 0 ]; then
		mptcp_lib_pr_fail "New MPTCP socket cannot be blocked via sysctl"
		mptcp_lib_result_fail "New MPTCP socket cannot be blocked via sysctl"
		ret=${KSFT_FAIL}
		return 1
	fi

	mptcp_lib_pr_ok
	mptcp_lib_result_pass "New MPTCP socket can be blocked via sysctl"
	return 0
}

do_ping()
{
	local listener_ns="$1"
	local connector_ns="$2"
	local connect_addr="$3"
	local ping_args="-q -c 1"
	local rc=0

	if mptcp_lib_is_v6 "${connect_addr}"; then
		$ipv6 || return 0
		ping_args="${ping_args} -6"
	fi

	ip netns exec ${connector_ns} ping ${ping_args} $connect_addr >/dev/null || rc=1

	if [ $rc -ne 0 ] ; then
		mptcp_lib_pr_fail "$listener_ns -> $connect_addr connectivity"
		ret=${KSFT_FAIL}

		return 1
	fi

	return 0
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

	port=$((port + 1))

	if [ "$rcvbuf" -gt 0 ]; then
		extra_args+=" -R $rcvbuf"
	fi

	if [ "$sndbuf" -gt 0 ]; then
		extra_args+=" -S $sndbuf"
	fi

	if [ -n "$testmode" ]; then
		extra_args+=" -m $testmode"
	fi

	if [ -n "$extra_args" ] && $options_log; then
		mptcp_lib_pr_info "extra options: $extra_args"
	fi
	options_log=false

	:> "$cout"
	:> "$sout"
	:> "$capout"

	local addr_port
	addr_port=$(printf "%s:%d" ${connect_addr} ${port})
	local result_msg
	result_msg="$(printf "%.3s %-5s -> %.3s (%-20s) %-5s" ${connector_ns} ${cl_proto} ${listener_ns} ${addr_port} ${srv_proto})"
	mptcp_lib_print_title "${result_msg}"

	if $capture; then
		local capuser
		local rndh="${connector_ns:4}"
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

	local stat_synrx_last_l
	local stat_ackrx_last_l
	local stat_cookietx_last
	local stat_cookierx_last
	local stat_csum_err_s
	local stat_csum_err_c
	local stat_tcpfb_last_l
	stat_synrx_last_l=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtMPCapableSYNRX")
	stat_ackrx_last_l=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtMPCapableACKRX")
	stat_cookietx_last=$(mptcp_lib_get_counter "${listener_ns}" "TcpExtSyncookiesSent")
	stat_cookierx_last=$(mptcp_lib_get_counter "${listener_ns}" "TcpExtSyncookiesRecv")
	stat_csum_err_s=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtDataCsumErr")
	stat_csum_err_c=$(mptcp_lib_get_counter "${connector_ns}" "MPTcpExtDataCsumErr")
	stat_tcpfb_last_l=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtMPCapableFallbackACK")

	timeout ${timeout_test} \
		ip netns exec ${listener_ns} \
			./mptcp_connect -t ${timeout_poll} -l -p $port -s ${srv_proto} \
				$extra_args $local_addr < "$sin" > "$sout" &
	local spid=$!

	mptcp_lib_wait_local_port_listen "${listener_ns}" "${port}"

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
	result_msg+=" # time=${duration}ms"
	printf "(duration %05sms) " "${duration}"
	if [ ${rets} -ne 0 ] || [ ${retc} -ne 0 ]; then
		mptcp_lib_pr_fail "client exit code $retc, server $rets"
		echo -e "\nnetns ${listener_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${listener_ns} ss -Menita 1>&2 -o "sport = :$port"
		cat /tmp/${listener_ns}.out
		echo -e "\nnetns ${connector_ns} socket stat for ${port}:" 1>&2
		ip netns exec ${connector_ns} ss -Menita 1>&2 -o "dport = :$port"
		[ ${listener_ns} != ${connector_ns} ] && cat /tmp/${connector_ns}.out

		echo
		cat "$capout"
		mptcp_lib_result_fail "${TEST_GROUP}: ${result_msg}"
		return 1
	fi

	mptcp_lib_check_transfer $sin $cout "file received by client"
	retc=$?
	mptcp_lib_check_transfer $cin $sout "file received by server"
	rets=$?

	local extra=""
	local stat_synrx_now_l
	local stat_ackrx_now_l
	local stat_cookietx_now
	local stat_cookierx_now
	local stat_ooo_now
	local stat_tcpfb_now_l
	stat_synrx_now_l=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtMPCapableSYNRX")
	stat_ackrx_now_l=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtMPCapableACKRX")
	stat_cookietx_now=$(mptcp_lib_get_counter "${listener_ns}" "TcpExtSyncookiesSent")
	stat_cookierx_now=$(mptcp_lib_get_counter "${listener_ns}" "TcpExtSyncookiesRecv")
	stat_ooo_now=$(mptcp_lib_get_counter "${listener_ns}" "TcpExtTCPOFOQueue")
	stat_tcpfb_now_l=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtMPCapableFallbackACK")

	expect_synrx=$((stat_synrx_last_l))
	expect_ackrx=$((stat_ackrx_last_l))

	cookies=$(ip netns exec ${listener_ns} sysctl net.ipv4.tcp_syncookies)
	cookies=${cookies##*=}

	if [ ${cl_proto} = "MPTCP" ] && [ ${srv_proto} = "MPTCP" ]; then
		expect_synrx=$((stat_synrx_last_l+connect_per_transfer))
		expect_ackrx=$((stat_ackrx_last_l+connect_per_transfer))
	fi

	if [ ${stat_synrx_now_l} -lt ${expect_synrx} ]; then
		mptcp_lib_pr_fail "lower MPC SYN rx (${stat_synrx_now_l})" \
				  "than expected (${expect_synrx})"
		retc=1
	fi
	if [ ${stat_ackrx_now_l} -lt ${expect_ackrx} ] && [ ${stat_ooo_now} -eq 0 ]; then
		if [ ${stat_ooo_now} -eq 0 ]; then
			mptcp_lib_pr_fail "lower MPC ACK rx (${stat_ackrx_now_l})" \
					  "than expected (${expect_ackrx})"
			rets=1
		else
			extra+=" [ Note ] fallback due to TCP OoO"
		fi
	fi

	if $checksum; then
		local csum_err_s
		local csum_err_c
		csum_err_s=$(mptcp_lib_get_counter "${listener_ns}" "MPTcpExtDataCsumErr")
		csum_err_c=$(mptcp_lib_get_counter "${connector_ns}" "MPTcpExtDataCsumErr")

		local csum_err_s_nr=$((csum_err_s - stat_csum_err_s))
		if [ $csum_err_s_nr -gt 0 ]; then
			mptcp_lib_pr_fail "server got ${csum_err_s_nr} data checksum error[s]"
			rets=1
		fi

		local csum_err_c_nr=$((csum_err_c - stat_csum_err_c))
		if [ $csum_err_c_nr -gt 0 ]; then
			mptcp_lib_pr_fail "client got ${csum_err_c_nr} data checksum error[s]"
			retc=1
		fi
	fi

	if [ ${stat_ooo_now} -eq 0 ] && [ ${stat_tcpfb_last_l} -ne ${stat_tcpfb_now_l} ]; then
		mptcp_lib_pr_fail "unexpected fallback to TCP"
		rets=1
	fi

	if [ $cookies -eq 2 ];then
		if [ $stat_cookietx_last -ge $stat_cookietx_now ] ;then
			extra+=" WARN: CookieSent: did not advance"
		fi
		if [ $stat_cookierx_last -ge $stat_cookierx_now ] ;then
			extra+=" WARN: CookieRecv: did not advance"
		fi
	else
		if [ $stat_cookietx_last -ne $stat_cookietx_now ] ;then
			extra+=" WARN: CookieSent: changed"
		fi
		if [ $stat_cookierx_last -ne $stat_cookierx_now ] ;then
			extra+=" WARN: CookieRecv: changed"
		fi
	fi

	if [ ${stat_synrx_now_l} -gt ${expect_synrx} ]; then
		extra+=" WARN: SYNRX: expect ${expect_synrx},"
		extra+=" got ${stat_synrx_now_l} (probably retransmissions)"
	fi
	if [ ${stat_ackrx_now_l} -gt ${expect_ackrx} ]; then
		extra+=" WARN: ACKRX: expect ${expect_ackrx},"
		extra+=" got ${stat_ackrx_now_l} (probably retransmissions)"
	fi

	if [ $retc -eq 0 ] && [ $rets -eq 0 ]; then
		mptcp_lib_pr_ok "${extra:1}"
		mptcp_lib_result_pass "${TEST_GROUP}: ${result_msg}"
	else
		if [ -n "${extra}" ]; then
			mptcp_lib_print_warn "${extra:1}"
		fi
		mptcp_lib_result_fail "${TEST_GROUP}: ${result_msg}"
	fi

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

	mptcp_lib_make_file $name 1024 $ksize
	dd if=/dev/urandom conv=notrunc of="$name" oflag=append bs=1 count=$rem 2> /dev/null

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
	if ! $ipv6 && mptcp_lib_is_v6 "${connect_addr}"; then
		return 0
	fi

	local local_addr
	if mptcp_lib_is_v6 "${connect_addr}"; then
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

	TEST_GROUP="${msg}"

	# skip if we don't want v6
	if ! $ipv6 && mptcp_lib_is_v6 "${connect_addr}"; then
		return 0
	fi

	# IP(V6)_TRANSPARENT has been added after TOS support which came with
	# the required infrastructure in MPTCP sockopt code. To support TOS, the
	# following function has been exported (T). Not great but better than
	# checking for a specific kernel version.
	if ! mptcp_lib_kallsyms_has "T __ip_sock_set_tos$"; then
		mptcp_lib_pr_skip "${msg} not supported by the kernel"
		mptcp_lib_result_skip "${TEST_GROUP}"
		return
	fi

	if ! ip netns exec "$listener_ns" nft -f /dev/stdin <<"EOF"
flush ruleset
table inet mangle {
	chain divert {
		type filter hook prerouting priority -150;

		meta l4proto tcp socket transparent 1 meta mark set 1 accept
		tcp dport 20000 tproxy to :20000 meta mark set 1 accept
	}
}
EOF
	then
		mptcp_lib_pr_skip "$msg, could not load nft ruleset"
		mptcp_lib_fail_if_expected_feature "nft rules"
		mptcp_lib_result_skip "${TEST_GROUP}"
		return
	fi

	local local_addr
	if mptcp_lib_is_v6 "${connect_addr}"; then
		local_addr="::"
		r6flag="-6"
	else
		local_addr="0.0.0.0"
	fi

	if ! ip -net "$listener_ns" $r6flag rule add fwmark 1 lookup 100; then
		ip netns exec "$listener_ns" nft flush ruleset
		mptcp_lib_pr_skip "$msg, ip $r6flag rule failed"
		mptcp_lib_fail_if_expected_feature "ip rule"
		mptcp_lib_result_skip "${TEST_GROUP}"
		return
	fi

	if ! ip -net "$listener_ns" route add local $local_addr/0 dev lo table 100; then
		ip netns exec "$listener_ns" nft flush ruleset
		ip -net "$listener_ns" $r6flag rule del fwmark 1 lookup 100
		mptcp_lib_pr_skip "$msg, ip route add local $local_addr failed"
		mptcp_lib_fail_if_expected_feature "ip route"
		mptcp_lib_result_skip "${TEST_GROUP}"
		return
	fi

	mptcp_lib_pr_info "test $msg"

	port=$((20000 - 1))
	local extra_args="-o TRANSPARENT"
	do_transfer ${listener_ns} ${connector_ns} MPTCP MPTCP \
		    ${connect_addr} ${local_addr} "${extra_args}"
	lret=$?

	ip netns exec "$listener_ns" nft flush ruleset
	ip -net "$listener_ns" $r6flag rule del fwmark 1 lookup 100
	ip -net "$listener_ns" route del local $local_addr/0 dev lo table 100

	if [ $lret -ne 0 ]; then
		mptcp_lib_pr_fail "$msg, mptcp connection error"
		ret=$lret
		return 1
	fi

	mptcp_lib_pr_info "$msg pass"
	return 0
}

run_tests_peekmode()
{
	local peekmode="$1"

	TEST_GROUP="peek mode: ${peekmode}"
	mptcp_lib_pr_info "with peek mode: ${peekmode}"
	run_tests_lo "$ns1" "$ns1" 10.0.1.1 1 "-P ${peekmode}"
	run_tests_lo "$ns1" "$ns1" dead:beef:1::1 1 "-P ${peekmode}"
}

run_tests_mptfo()
{
	TEST_GROUP="MPTFO"

	if ! mptcp_lib_kallsyms_has "mptcp_fastopen_"; then
		mptcp_lib_pr_skip "TFO not supported by the kernel"
		mptcp_lib_result_skip "${TEST_GROUP}"
		return
	fi

	mptcp_lib_pr_info "with MPTFO start"
	ip netns exec "$ns1" sysctl -q net.ipv4.tcp_fastopen=2
	ip netns exec "$ns2" sysctl -q net.ipv4.tcp_fastopen=1

	run_tests_lo "$ns1" "$ns2" 10.0.1.1 0 "-o MPTFO"
	run_tests_lo "$ns1" "$ns2" 10.0.1.1 0 "-o MPTFO"

	run_tests_lo "$ns1" "$ns2" dead:beef:1::1 0 "-o MPTFO"
	run_tests_lo "$ns1" "$ns2" dead:beef:1::1 0 "-o MPTFO"

	ip netns exec "$ns1" sysctl -q net.ipv4.tcp_fastopen=0
	ip netns exec "$ns2" sysctl -q net.ipv4.tcp_fastopen=0
	mptcp_lib_pr_info "with MPTFO end"
}

run_tests_disconnect()
{
	local old_cin=$cin
	local old_sin=$sin

	TEST_GROUP="full disconnect"

	if ! mptcp_lib_kallsyms_has "mptcp_pm_data_reset$"; then
		mptcp_lib_pr_skip "Full disconnect not supported"
		mptcp_lib_result_skip "${TEST_GROUP}"
		return
	fi

	cat $cin $cin $cin > "$cin".disconnect

	# force do_transfer to cope with the multiple transmissions
	sin="$cin.disconnect"
	cin="$cin.disconnect"
	cin_disconnect="$old_cin"
	connect_per_transfer=3

	mptcp_lib_pr_info "disconnect"
	run_tests_lo "$ns1" "$ns1" 10.0.1.1 1 "-I 3 -i $old_cin"
	run_tests_lo "$ns1" "$ns1" dead:beef:1::1 1 "-I 3 -i $old_cin"

	# restore previous status
	sin=$old_sin
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

log_if_error()
{
	local msg="$1"

	if [ ${ret} -ne 0 ]; then
		mptcp_lib_pr_fail "${msg}"

		final_ret=${ret}
		ret=${KSFT_PASS}

		return ${final_ret}
	fi
}

stop_if_error()
{
	if ! log_if_error "${@}"; then
		display_time
		mptcp_lib_result_print_all_tap
		exit ${final_ret}
	fi
}

make_file "$cin" "client"
make_file "$sin" "server"

check_mptcp_disabled

stop_if_error "The kernel configuration is not valid for MPTCP"

print_larger_title "Validating network environment with pings"
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

mptcp_lib_result_code "${ret}" "ping tests"

stop_if_error "Could not even run ping tests"
mptcp_lib_pr_ok

[ -n "$tc_loss" ] && tc -net "$ns2" qdisc add dev ns2eth3 root netem loss random $tc_loss delay ${tc_delay}ms
tc_info="loss of $tc_loss "
test "$tc_delay" -gt 0 && tc_info+="delay $tc_delay ms "

reorder_delay=$((tc_delay / 4))

if [ -z "${tc_reorder}" ]; then
	reorder1=$((RANDOM%10))
	reorder1=$((100 - reorder1))
	reorder2=$((RANDOM%100))

	if [ $reorder_delay -gt 0 ] && [ $reorder1 -lt 100 ] && [ $reorder2 -gt 0 ]; then
		tc_reorder="reorder ${reorder1}% ${reorder2}%"
		tc_info+="$tc_reorder with delay ${reorder_delay}ms "
	fi
elif [ "$tc_reorder" = "0" ];then
	tc_reorder=""
elif [ "$reorder_delay" -gt 0 ];then
	# reordering requires some delay
	tc_reorder="reorder $tc_reorder"
	tc_info+="$tc_reorder with delay ${reorder_delay}ms "
fi

mptcp_lib_pr_info "Using ${tc_info}on ns3eth4"

tc -net "$ns3" qdisc add dev ns3eth4 root netem delay ${reorder_delay}ms $tc_reorder

TEST_GROUP="loopback v4"
run_tests_lo "$ns1" "$ns1" 10.0.1.1 1
stop_if_error "Could not even run loopback test"

TEST_GROUP="loopback v6"
run_tests_lo "$ns1" "$ns1" dead:beef:1::1 1
stop_if_error "Could not even run loopback v6 test"

TEST_GROUP="multihosts"
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

	log_if_error "Tests with $sender as a sender have failed"
done

run_tests_peekmode "saveWithPeek"
run_tests_peekmode "saveAfterPeek"
log_if_error "Tests with peek mode have failed"

# MPTFO (MultiPath TCP Fatopen tests)
run_tests_mptfo
log_if_error "Tests with MPTFO have failed"

# connect to ns4 ip address, ns2 should intercept/proxy
run_test_transparent 10.0.3.1 "tproxy ipv4"
run_test_transparent dead:beef:3::1 "tproxy ipv6"
log_if_error "Tests with tproxy have failed"

run_tests_disconnect
log_if_error "Tests of the full disconnection have failed"

display_time
mptcp_lib_result_print_all_tap
exit ${final_ret}
