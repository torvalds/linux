#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

checktool "conntrack --version" "run test without conntrack"
checktool "iptables --version" "run test without iptables"
checktool "ip6tables --version" "run test without ip6tables"

modprobe -q tun
modprobe -q nf_conntrack
# echo 1 > /proc/sys/net/netfilter/nf_log_all_netns

PDRILL_TIMEOUT=10

files="
conntrack_ack_loss_stall.pkt
conntrack_inexact_rst.pkt
conntrack_syn_challenge_ack.pkt
conntrack_synack_old.pkt
conntrack_synack_reuse.pkt
conntrack_rst_invalid.pkt
"

if ! packetdrill --dry_run --verbose "packetdrill/conntrack_ack_loss_stall.pkt";then
	echo "SKIP: packetdrill not installed"
	exit ${ksft_skip}
fi

ret=0

run_packetdrill()
{
	filename="$1"
	ipver="$2"
	local mtu=1500

	export NFCT_IP_VERSION="$ipver"

	if [ "$ipver" = "ipv4" ];then
		export xtables="iptables"
	elif [ "$ipver" = "ipv6" ];then
		export xtables="ip6tables"
		mtu=1520
	fi

	timeout "$PDRILL_TIMEOUT" unshare -n packetdrill --ip_version="$ipver" --mtu=$mtu \
		--tolerance_usecs=1000000 --non_fatal packet "$filename"
}

run_one_test_file()
{
	filename="$1"

	for v in ipv4 ipv6;do
		printf "%-50s(%s)%-20s" "$filename" "$v" ""
		if run_packetdrill packetdrill/"$f" "$v";then
			echo OK
		else
			echo FAIL
			ret=1
		fi
	done
}

echo "Replaying packetdrill test cases:"
for f in $files;do
	run_one_test_file packetdrill/"$f"
done

exit $ret
