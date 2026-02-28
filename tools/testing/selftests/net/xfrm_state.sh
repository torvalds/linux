#!/bin/bash -e
# SPDX-License-Identifier: GPL-2.0
#
# xfrm/IPsec tests.
# Currently implemented:
# - ICMP error source address verification (IETF RFC 4301 section 6)
# - ICMP MTU exceeded handling over IPsec tunnels.
#
# Addresses and topology:
# IPv4 prefix 10.1.c.d IPv6 prefix fc00:c::d/64 where c is the segment number
# and d is the interface identifier.
# IPv6 uses the same c:d as IPv4, and start with IPv6 prefix instead ipv4 prefix
#
# Network topology default: ns_set_v4 or ns_set_v6
#   1.1   1.2   2.1   2.2   3.1   3.2   4.1   4.2   5.1   5.2  6.1  6.2
#  eth0  eth1  eth0  eth1  eth0  eth1  eth0  eth1  eth0  eth1 eth0  eth1
# a -------- r1 -------- s1 -------- r2 -------- s2 -------- r3 -------- b
# a, b = Alice and Bob hosts without IPsec.
# r1, r2, r3 routers, without IPsec
# s1, s2, IPsec gateways/routers that setup tunnel(s).

# Network topology x: IPsec gateway that generates ICMP response - ns_set_v4x or ns_set_v6x
#   1.1   1.2   2.1   2.2   3.1   3.2   4.1   4.2   5.1   5.2
#  eth0  eth1  eth0  eth1  eth0  eth1  eth0  eth1  eth0  eth1
# a -------- r1 -------- s1 -------- r2 -------- s2 -------- b

. lib.sh

EXIT_ON_TEST_FAIL=no
PAUSE=no
VERBOSE=${VERBOSE:-0}
DEBUG=0

#	Name				Description
tests="
	unreachable_ipv4		IPv4 unreachable from router r3
	unreachable_ipv6		IPv6 unreachable from router r3
	unreachable_gw_ipv4		IPv4 unreachable from IPsec gateway s2
	unreachable_gw_ipv6		IPv6 unreachable from IPsec gateway s2
	mtu_ipv4_s2			IPv4 MTU exceeded from IPsec gateway s2
	mtu_ipv6_s2			IPv6 MTU exceeded from IPsec gateway s2
	mtu_ipv4_r2			IPv4 MTU exceeded from ESP router r2
	mtu_ipv6_r2			IPv6 MTU exceeded from ESP router r2
	mtu_ipv4_r3			IPv4 MTU exceeded from router r3
	mtu_ipv6_r3			IPv6 MTU exceeded from router r3"

prefix4="10.1"
prefix6="fc00"

run_cmd_err() {
	cmd="$*"

	if [ "$VERBOSE" -gt 0 ]; then
		printf "  COMMAND: %s\n" "$cmd"
	fi

	out="$($cmd 2>&1)" && rc=0 || rc=$?
	if [ "$VERBOSE" -gt 1 ] && [ -n "$out" ]; then
		echo "  $out"
		echo
	fi
	return 0
}

run_cmd() {
	run_cmd_err "$@" || exit 1
}

run_test() {
	# If errexit is set, unset it for sub-shell and restore after test
	errexit=0
	if [[ $- =~ "e" ]]; then
		errexit=1
		set +e
	fi

	(
		unset IFS

		# shellcheck disable=SC2030 # fail is read by trap/cleanup within this subshell
		fail="yes"

		# Since cleanup() relies on variables modified by this sub shell,
		# it has to run in this context.
		trap 'log_test_error $?; cleanup' EXIT INT TERM

		if [ "$VERBOSE" -gt 0 ]; then
			printf "\n#############################################################\n\n"
		fi

		ret=0
		case "${name}" in
		# can't use eval and test names shell check will complain about unused code
		unreachable_ipv4)    test_unreachable_ipv4 ;;
		unreachable_ipv6)    test_unreachable_ipv6 ;;
		unreachable_gw_ipv4) test_unreachable_gw_ipv4 ;;
		unreachable_gw_ipv6) test_unreachable_gw_ipv6 ;;
		mtu_ipv4_s2)         test_mtu_ipv4_s2 ;;
		mtu_ipv6_s2)         test_mtu_ipv6_s2 ;;
		mtu_ipv4_r2)         test_mtu_ipv4_r2 ;;
		mtu_ipv6_r2)         test_mtu_ipv6_r2 ;;
		mtu_ipv4_r3)         test_mtu_ipv4_r3 ;;
		mtu_ipv6_r3)         test_mtu_ipv6_r3 ;;
		esac
		ret=$?

		if [ $ret -eq 0 ]; then
			fail="no"

			if [ "$VERBOSE" -gt 1 ]; then
				show_icmp_filter
			fi

			printf "TEST: %-60s [ PASS ]\n" "${desc}"
		elif [ $ret -eq "$ksft_skip" ]; then
			fail="no"
			printf "TEST: %-60s [SKIP]\n" "${desc}"
		fi

		return $ret
	)
	ret=$?

	[ $errexit -eq 1 ] && set -e

	case $ret in
	0)
		all_skipped=false
		[ "$exitcode" -eq "$ksft_skip" ] && exitcode=0
		;;
	"$ksft_skip")
		[ $all_skipped = true ] && exitcode=$ksft_skip
		;;
	*)
		all_skipped=false
		exitcode=1
		;;
	esac

	return 0 # don't trigger errexit (-e); actual status in exitcode
}

setup_namespaces() {
	local namespaces=""

	NS_A=""
	NS_B=""
	NS_R1=""
	NS_R2=""
	NS_R3=""
	NS_S1=""
	NS_S2=""

	for ns in ${ns_set}; do
		namespaces="$namespaces NS_${ns^^}"
	done

	# shellcheck disable=SC2086 # setup_ns expects unquoted list
	setup_ns $namespaces

	ns_active= #ordered list of namespaces for this test.

	[ -n "${NS_A}" ] && ns_a=(ip netns exec "${NS_A}") && ns_active="${ns_active} $NS_A"
	[ -n "${NS_R1}" ] && ns_active="${ns_active} $NS_R1"
	[ -n "${NS_S1}" ] && ns_s1=(ip netns exec "${NS_S1}") && ns_active="${ns_active} $NS_S1"
	[ -n "${NS_R2}" ] && ns_r2=(ip netns exec "${NS_R2}") && ns_active="${ns_active} $NS_R2"
	[ -n "${NS_S2}" ] && ns_s2=(ip netns exec "${NS_S2}") && ns_active="${ns_active} $NS_S2"
	[ -n "${NS_R3}" ] && ns_r3=(ip netns exec "${NS_R3}") && ns_active="${ns_active} $NS_R3"
	[ -n "${NS_B}" ] && ns_active="${ns_active} $NS_B"
}

addr_add() {
	local -a ns_cmd=(ip netns exec "$1")
	local addr="$2"
	local dev="$3"

	run_cmd "${ns_cmd[@]}" ip addr add "${addr}" dev "${dev}"
	run_cmd "${ns_cmd[@]}" ip link set up "${dev}"
}

veth_add() {
	local ns=$2
	local pns=$1
	local -a ns_cmd=(ip netns exec "${pns}")
	local ln="eth0"
	local rn="eth1"

	run_cmd "${ns_cmd[@]}" ip link add "${ln}" type veth peer name "${rn}" netns "${ns}"
}

show_icmp_filter() {
	run_cmd "${ns_r2[@]}" nft list ruleset
	echo "$out"
}

setup_icmp_filter() {
	run_cmd "${ns_r2[@]}" nft add table inet filter
	run_cmd "${ns_r2[@]}" nft add chain inet filter FORWARD \
		'{ type filter hook forward priority filter; policy drop ; }'
	run_cmd "${ns_r2[@]}" nft add rule inet filter FORWARD counter ip protocol esp \
		counter log accept
	run_cmd "${ns_r2[@]}" nft add rule inet filter FORWARD counter ip protocol \
		icmp counter log drop

	if [ "$VERBOSE" -gt 0 ]; then
		run_cmd "${ns_r2[@]}" nft list ruleset
		echo "$out"
	fi
}

setup_icmpv6_filter() {
	run_cmd "${ns_r2[@]}" nft add table inet filter
	run_cmd "${ns_r2[@]}" nft add chain inet filter FORWARD \
		'{ type filter hook forward priority filter; policy drop ; }'
	run_cmd "${ns_r2[@]}" nft add rule inet filter FORWARD ip6 nexthdr \
		ipv6-icmp icmpv6 type echo-request counter log drop
	run_cmd "${ns_r2[@]}" nft add rule inet filter FORWARD ip6 nexthdr esp \
		counter log accept
	run_cmd "${ns_r2[@]}" nft add rule inet filter FORWARD ip6 nexthdr \
		ipv6-icmp icmpv6 type \
		'{nd-neighbor-solicit,nd-neighbor-advert,nd-router-solicit,nd-router-advert}' \
		counter log drop
	if [ "$VERBOSE" -gt 0 ]; then
		run_cmd "${ns_r2[@]}" nft list ruleset
		echo "$out"
	fi
}

set_xfrm_params() {
	s1_src=${src}
	s1_dst=${dst}
	s1_src_net=${src_net}
	s1_dst_net=${dst_net}
}

setup_ns_set_v4() {
	ns_set="a r1 s1 r2 s2 r3 b"    # Network topology default
	imax=$(echo "$ns_set" | wc -w) # number of namespaces in this topology

	src="10.1.3.1"
	dst="10.1.4.2"
	src_net="10.1.1.0/24"
	dst_net="10.1.6.0/24"

	prefix=${prefix4}
	prefix_len=24
	s="."
	S="."

	set_xfrm_params
}

setup_ns_set_v4x() {
	ns_set="a r1 s1 r2 s2 b"       # Network topology: x
	imax=$(echo "$ns_set" | wc -w) # number of namespaces in this topology
	prefix=${prefix4}
	s="."
	S="."
	src="10.1.3.1"
	dst="10.1.4.2"
	src_net="10.1.1.0/24"
	dst_net="10.1.5.0/24"
	prefix_len=24

	set_xfrm_params
}

setup_ns_set_v6() {
	ns_set="a r1 s1 r2 s2 r3 b"    # Network topology default
	imax=$(echo "$ns_set" | wc -w) # number of namespaces in this topology
	prefix=${prefix6}
	s=":"
	S="::"
	src="fc00:3::1"
	dst="fc00:4::2"
	src_net="fc00:1::0/64"
	dst_net="fc00:6::0/64"
	prefix_len=64

	set_xfrm_params
}

setup_ns_set_v6x() {
	ns_set="a r1 s1 r2 s2 b" # Network topology: x
	imax=$(echo "$ns_set" | wc -w)
	prefix=${prefix6}
	s=":"
	S="::"
	src="fc00:3::1"
	dst="fc00:4::2"
	src_net="fc00:1::0/64"
	dst_net="fc00:5::0/64"
	prefix_len=64

	set_xfrm_params
}

setup_network() {
	# Create veths and add addresses
	local -a ns_cmd
	i=1
	p=""
	for ns in ${ns_active}; do
		ns_cmd=(ip netns exec "${ns}")

		if [ "${i}" -ne 1 ]; then
			# Create veth between previous and current namespace
			veth_add "${p}" "${ns}"
			# Add addresses: previous gets .1 on eth0, current gets .2 on eth1
			addr_add "${p}" "${prefix}${s}$((i-1))${S}1/${prefix_len}" eth0
			addr_add "${ns}" "${prefix}${s}$((i-1))${S}2/${prefix_len}" eth1
		fi

		# Enable forwarding
		run_cmd "${ns_cmd[@]}" sysctl -q net/ipv4/ip_forward=1
		run_cmd "${ns_cmd[@]}" sysctl -q net/ipv6/conf/all/forwarding=1
		run_cmd "${ns_cmd[@]}" sysctl -q net/ipv6/conf/default/accept_dad=0

		p=${ns}
		i=$((i + 1))
	done

	# Add routes (needs all addresses to exist first)
	i=1
	for ns in ${ns_active}; do
		ns_cmd=(ip netns exec "${ns}")

		# Forward routes to networks beyond this node
		if [ "${i}" -ne "${imax}" ]; then
			nhf="${prefix}${s}${i}${S}2" # nexthop forward
			for j in $(seq $((i + 1)) "${imax}"); do
				run_cmd "${ns_cmd[@]}" ip route replace \
				       	"${prefix}${s}${j}${S}0/${prefix_len}" via "${nhf}"
			done
		fi

		# Reverse routes to networks before this node
		if [ "${i}" -gt 1 ]; then
			nhr="${prefix}${s}$((i-1))${S}1" # nexthop reverse
			for j in $(seq 1 $((i - 2))); do
				run_cmd "${ns_cmd[@]}" ip route replace \
					"${prefix}${s}${j}${S}0/${prefix_len}" via "${nhr}"
			done
		fi

		i=$((i + 1))
	done
}

setup_xfrm_mode() {
	local MODE=${1:-tunnel}
	if [ "${MODE}" != "tunnel" ] && [ "${MODE}" != "beet" ]; then
		echo "xfrm mode ${MODE} not supported"
		log_test_error
		return 1
	fi

	run_cmd "${ns_s1[@]}" ip xfrm policy add src "${s1_src_net}" dst "${s1_dst_net}" dir out \
		tmpl src "${s1_src}" dst "${s1_dst}" proto esp reqid 1 mode "${MODE}"

	# no "input" policies. we are only doing forwarding so far

	run_cmd "${ns_s1[@]}" ip xfrm policy add src "${s1_dst_net}" dst "${s1_src_net}" dir fwd \
		flag icmp tmpl src "${s1_dst}" dst "${s1_src}" proto esp reqid 2 mode "${MODE}"

	run_cmd "${ns_s1[@]}" ip xfrm state add src "${s1_src}" dst "${s1_dst}" proto esp spi 1 \
		reqid 1 mode "${MODE}" aead 'rfc4106(gcm(aes))' \
		0x1111111111111111111111111111111111111111 96 \
		sel src "${s1_src_net}" dst "${s1_dst_net}" dir out

	run_cmd "${ns_s1[@]}" ip xfrm state add src "${s1_dst}" dst "${s1_src}" proto esp spi 2 \
		reqid 2 flag icmp replay-window 8 mode "${MODE}" aead 'rfc4106(gcm(aes))' \
		0x2222222222222222222222222222222222222222 96 \
		sel src "${s1_dst_net}" dst "${s1_src_net}" dir in

	run_cmd "${ns_s2[@]}" ip xfrm policy add src "${s1_dst_net}" dst "${s1_src_net}" dir out \
		flag icmp tmpl src "${s1_dst}" dst "${s1_src}" proto esp reqid 2 mode "${MODE}"

	run_cmd "${ns_s2[@]}" ip xfrm policy add src "${s1_src_net}" dst "${s1_dst_net}" dir fwd \
		tmpl src "${s1_src}" dst "${s1_dst}" proto esp reqid 1 mode "${MODE}"

	run_cmd "${ns_s2[@]}" ip xfrm state add src "${s1_dst}" dst "${s1_src}" proto esp spi 2 \
		reqid 2 mode "${MODE}" aead 'rfc4106(gcm(aes))' \
		0x2222222222222222222222222222222222222222 96 \
		sel src "${s1_dst_net}" dst "${s1_src_net}" dir out

	run_cmd "${ns_s2[@]}" ip xfrm state add src "${s1_src}" dst "${s1_dst}" proto esp spi 1 \
		reqid 1 flag icmp replay-window 8 mode "${MODE}" aead 'rfc4106(gcm(aes))' \
		0x1111111111111111111111111111111111111111 96 \
		sel src "${s1_src_net}" dst "${s1_dst_net}" dir in
}

setup_xfrm() {
	setup_xfrm_mode tunnel
}

setup() {
	[ "$(id -u)" -ne 0 ] && echo "  need to run as root" && return "$ksft_skip"

	for arg; do
		case "${arg}" in
		ns_set_v4)     setup_ns_set_v4 ;;
		ns_set_v4x)    setup_ns_set_v4x ;;
		ns_set_v6)     setup_ns_set_v6 ;;
		ns_set_v6x)    setup_ns_set_v6x ;;
		namespaces)    setup_namespaces ;;
		network)       setup_network ;;
		xfrm)          setup_xfrm ;;
		icmp_filter)   setup_icmp_filter ;;
		icmpv6_filter) setup_icmpv6_filter ;;
		*) echo "  ${arg} not supported"; return 1 ;;
		esac || return 1
	done
}

# shellcheck disable=SC2317 # called via trap
pause() {
	echo
	echo "Pausing. Hit enter to continue"
	read -r _
}

# shellcheck disable=SC2317 # called via trap
log_test_error() {
	# shellcheck disable=SC2031 # fail is set in subshell, read via trap
	if [ "${fail}" = "yes" ] && [ -n "${desc}" ]; then
		if [ "$VERBOSE" -gt 0 ]; then
			show_icmp_filter
		fi
		printf "TEST: %-60s [ FAIL ]  %s\n" "${desc}" "${name}"
		[ -n "${cmd}" ] && printf '%s\n\n' "${cmd}"
		[ -n "${out}" ] && printf '%s\n\n' "${out}"
	fi
}

# shellcheck disable=SC2317 # called via trap
cleanup() {
	# shellcheck disable=SC2031 # fail is set in subshell, read via trap
	[[ "$PAUSE" = "always" || ( "$PAUSE" = "fail" && "$fail" = "yes" ) ]] && pause
	cleanup_all_ns
	# shellcheck disable=SC2031 # fail is set in subshell, read via trap
	[ "${EXIT_ON_TEST_FAIL}" = "yes" ] && [ "${fail}" = "yes" ] && exit 1
}

test_unreachable_ipv6() {
	setup ns_set_v6 namespaces network xfrm icmpv6_filter || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:6::2
	run_cmd_err "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:6::3
	rc=0
	echo -e "$out" | grep -q -E 'From fc00:5::2 icmp_seq.* Destination' || rc=1
	return "${rc}"
}

test_unreachable_gw_ipv6() {
	setup ns_set_v6x namespaces network xfrm icmpv6_filter || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:5::2
	run_cmd_err "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:5::3
	rc=0
	echo -e "$out" | grep -q -E 'From fc00:4::2 icmp_seq.* Destination' || rc=1
	return "${rc}"
}

test_unreachable_ipv4() {
	setup ns_set_v4 namespaces network icmp_filter xfrm || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.6.2
	run_cmd_err "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.6.3
	rc=0
	echo -e "$out" | grep -q -E 'From 10.1.5.2 icmp_seq.* Destination' || rc=1
	return "${rc}"
}

test_unreachable_gw_ipv4() {
	setup ns_set_v4x namespaces network icmp_filter xfrm || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.5.2
	run_cmd_err "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.5.3
	rc=0
	echo -e "$out" | grep -q -E 'From 10.1.4.2 icmp_seq.* Destination' || rc=1
	return "${rc}"
}

test_mtu_ipv4_r2() {
	setup ns_set_v4 namespaces network icmp_filter xfrm || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.6.2
	run_cmd "${ns_r2[@]}" ip route replace 10.1.3.0/24 dev eth1 src 10.1.3.2 mtu 1300
	run_cmd "${ns_r2[@]}" ip route replace 10.1.4.0/24 dev eth0 src 10.1.4.1 mtu 1300
	# shellcheck disable=SC1010 # -M do: do = dont-fragment, not shell keyword
	run_cmd "${ns_a[@]}" ping -M do -s 1300 -W 5 -w 4 -c 1 10.1.6.2 || true
	rc=0
	echo -e "$out" | grep -q -E "From 10.1.2.2 icmp_seq=.* Frag needed and DF set" || rc=1
	return "${rc}"
}

test_mtu_ipv6_r2() {
	setup ns_set_v6 namespaces network xfrm icmpv6_filter || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:6::2
	run_cmd "${ns_r2[@]}" ip -6 route replace fc00:3::/64 \
		dev eth1 metric 256 src fc00:3::2 mtu 1300
	run_cmd "${ns_r2[@]}" ip -6 route replace fc00:4::/64 \
		dev eth0 metric 256 src fc00:4::1 mtu 1300
	# shellcheck disable=SC1010 # -M do: do = dont-fragment, not shell keyword
	run_cmd "${ns_a[@]}" ping -M do -s 1300 -W 5 -w 4 -c 1 fc00:6::2 || true
	rc=0
	echo -e "$out" | grep -q -E "From fc00:2::2 icmp_seq=.* Packet too big: mtu=1230" || rc=1
	return "${rc}"
}

test_mtu_ipv4_r3() {
	setup ns_set_v4 namespaces network icmp_filter xfrm || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.6.2
	run_cmd "${ns_r3[@]}" ip route replace 10.1.6.0/24 dev eth0 mtu 1300
	# shellcheck disable=SC1010 # -M do: do = dont-fragment, not shell keyword
	run_cmd "${ns_a[@]}" ping -M do -s 1350 -W 5 -w 4 -c 1 10.1.6.2 || true
	rc=0
	echo -e "$out" | grep -q -E "From 10.1.5.2 .* Frag needed and DF set \(mtu = 1300\)" || rc=1
	return "${rc}"
}

test_mtu_ipv4_s2() {
	setup ns_set_v4x namespaces network icmp_filter xfrm || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 10.1.5.2
	run_cmd "${ns_s2[@]}" ip route replace 10.1.5.0/24 dev eth0 src 10.1.5.1 mtu 1300
	# shellcheck disable=SC1010 # -M do: do = dont-fragment, not shell keyword
	run_cmd "${ns_a[@]}" ping -M do -s 1350 -W 5 -w 4 -c 1 10.1.5.2 || true
	rc=0
	echo -e "$out" | grep -q -E "From 10.1.4.2.*Frag needed and DF set \(mtu = 1300\)" || rc=1
	return "${rc}"
}

test_mtu_ipv6_s2() {
	setup ns_set_v6x namespaces network xfrm icmpv6_filter || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:5::2
	run_cmd "${ns_s2[@]}" ip -6 route replace fc00:5::/64 dev eth0 metric 256 mtu 1300
	# shellcheck disable=SC1010 # -M do: do = dont-fragment, not shell keyword
	run_cmd "${ns_a[@]}" ping -M do -s 1350 -W 5 -w 4 -c 1 fc00:5::2 || true
	rc=0
	echo -e "$out" | grep -q -E "From fc00:4::2.*Packet too big: mtu=1300" || rc=1
	return "${rc}"
}

test_mtu_ipv6_r3() {
	setup ns_set_v6 namespaces network xfrm icmpv6_filter || return "$ksft_skip"
	run_cmd "${ns_a[@]}" ping -W 5 -w 4 -c 1 fc00:6::2
	run_cmd "${ns_r3[@]}" ip -6 route replace fc00:6::/64 dev eth1 metric 256 mtu 1300
	# shellcheck disable=SC1010 # -M do: do = dont-fragment, not shell keyword
	run_cmd "${ns_a[@]}" ping -M do -s 1300 -W 5 -w 4 -c 1 fc00:6::2 || true
	rc=0
	echo -e "$out" | grep -q -E "From fc00:5::2 icmp_seq=.* Packet too big: mtu=1300" || rc=1
	return "${rc}"
}

################################################################################
#
usage() {
	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo -e "\t-p Pause on fail. Namespaces are kept for diagnostics"
	echo -e "\t-P Pause after the test. Namespaces are kept for diagnostics"
	echo -e "\t-v Verbose output. Show commands; -vv Show output and nft rules also"
	echo "Available tests${tests}"
	exit 1
}

################################################################################
#
exitcode=0
all_skipped=true
out=
cmd=

while getopts :epPv o; do
	case $o in
	e) EXIT_ON_TEST_FAIL=yes ;;
	P) PAUSE=always ;;
	p) PAUSE=fail ;;
	v) VERBOSE=$((VERBOSE + 1)) ;;
	*) usage ;;
	esac
done
shift $((OPTIND - 1))

IFS=$'\t\n'

for arg; do
	# Check first that all requested tests are available before running any
	command -v "test_${arg}" >/dev/null || {
		echo "=== Test ${arg} not found"
		usage
	}
done

name=""
desc=""
fail="no"

for t in ${tests}; do
	[ "${name}" = "" ] && name="${t}" && continue
	[ "${desc}" = "" ] && desc="${t}"

	run_this=1
	for arg; do
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	if [ $run_this -eq 1 ]; then
		run_test
	fi
	name=""
	desc=""
done

exit ${exitcode}
