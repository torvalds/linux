#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# return code to signal skipped test
ksft_skip=4
rc=0

source lib.sh

checktool "socat -h" "run test without socat"
checktool "iptables --version" "test needs iptables"

infile=$(mktemp)

cleanup()
{
	ip netns del "$netns"
	rm -f "$infile"
}

trap cleanup EXIT

setup_ns netns

ip -net "$netns" link add d0 type dummy
ip -net "$netns" link set d0 up
ip -net "$netns" addr add 10.1.2.1/24 dev d0

pattern="foo bar baz"
patlen=11
hdrlen=$((20 + 8)) # IPv4 + UDP

#ip netns exec "$netns" tcpdump -npXi d0 &
#tcpdump_pid=$!
#trap 'kill $tcpdump_pid; ip netns del $netns' EXIT

add_rule() { # (alg, from, to)
	ip netns exec "$netns" \
		iptables -A OUTPUT -o d0 -m string \
			--string "$pattern" --algo "$1" --from "$2" --to "$3"
}
showrules() { # ()
	ip netns exec "$netns" iptables -v -S OUTPUT | grep '^-A'
}
zerorules() {
	ip netns exec "$netns" iptables -Z OUTPUT
}
countrule() { # (pattern)
	showrules | grep -c -- "$*"
}
send() { # (offset)
	( for ((i = 0; i < $1 - hdrlen; i++)); do
		echo -n " "
	  done
	  echo -n "$pattern"
	) > "$infile"

	ip netns exec "$netns" socat -t 1 -u STDIN UDP-SENDTO:10.1.2.2:27374 < "$infile"
}

add_rule bm 1000 1500
add_rule bm 1400 1600
add_rule kmp 1000 1500
add_rule kmp 1400 1600

zerorules
send 0
send $((1000 - patlen))
if [ "$(countrule -c 0 0)" -ne 4 ]; then
	echo "FAIL: rules match data before --from"
	showrules
	((rc--))
fi

zerorules
send 1000
send $((1400 - patlen))
if [ "$(countrule -c 2)" -ne 2 ]; then
	echo "FAIL: only two rules should match at low offset"
	showrules
	((rc--))
fi

zerorules
send $((1500 - patlen))
if [ "$(countrule -c 1)" -ne 4 ]; then
	echo "FAIL: all rules should match at end of packet"
	showrules
	((rc--))
fi

zerorules
send 1495
if [ "$(countrule -c 1)" -ne 1 ]; then
	echo "FAIL: only kmp with proper --to should match pattern spanning fragments"
	showrules
	((rc--))
fi

zerorules
send 1500
if [ "$(countrule -c 1)" -ne 2 ]; then
	echo "FAIL: two rules should match pattern at start of second fragment"
	showrules
	((rc--))
fi

zerorules
send $((1600 - patlen))
if [ "$(countrule -c 1)" -ne 2 ]; then
	echo "FAIL: two rules should match pattern at end of largest --to"
	showrules
	((rc--))
fi

zerorules
send $((1600 - patlen + 1))
if [ "$(countrule -c 1)" -ne 0 ]; then
	echo "FAIL: no rules should match pattern extending largest --to"
	showrules
	((rc--))
fi

zerorules
send 1600
if [ "$(countrule -c 1)" -ne 0 ]; then
	echo "FAIL: no rule should match pattern past largest --to"
	showrules
	((rc--))
fi

[ $rc -eq 0 ] && echo "PASS: string match tests"
exit $rc
