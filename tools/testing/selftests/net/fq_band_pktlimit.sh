#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Verify that FQ has a packet limit per band:
#
# 1. set the limit to 10 per band
# 2. send 20 pkts on band A: verify that 10 are queued, 10 dropped
# 3. send 20 pkts on band A: verify that  0 are queued, 20 dropped
# 4. send 20 pkts on band B: verify that 10 are queued, 10 dropped
#
# Send packets with a delay to ensure that previously sent
# packets are still queued when later ones are sent.
# Use SO_TXTIME for this.

die() {
	echo "$1"
	exit 1
}

# run inside private netns
if [[ $# -eq 0 ]]; then
	./in_netns.sh "$0" __subprocess
	exit
fi

ip link add type dummy
ip link set dev dummy0 up
ip -6 addr add fdaa::1/128 dev dummy0
ip -6 route add fdaa::/64 dev dummy0
tc qdisc replace dev dummy0 root handle 1: fq quantum 1514 initial_quantum 1514 limit 10

DELAY=400000

./cmsg_sender -6 -p u -d "${DELAY}" -n 20 fdaa::2 8000
OUT1="$(tc -s qdisc show dev dummy0 | grep '^\ Sent')"

./cmsg_sender -6 -p u -d "${DELAY}" -n 20 fdaa::2 8000
OUT2="$(tc -s qdisc show dev dummy0 | grep '^\ Sent')"

./cmsg_sender -6 -p u -d "${DELAY}" -n 20 -P 7 fdaa::2 8000
OUT3="$(tc -s qdisc show dev dummy0 | grep '^\ Sent')"

# Initial stats will report zero sent, as all packets are still
# queued in FQ. Sleep for at least the delay period and see that
# twenty are now sent.
sleep 0.6
OUT4="$(tc -s qdisc show dev dummy0 | grep '^\ Sent')"

# Log the output after the test
echo "${OUT1}"
echo "${OUT2}"
echo "${OUT3}"
echo "${OUT4}"

# Test the output for expected values
echo "${OUT1}" | grep -q '0\ pkt\ (dropped\ 10'  || die "unexpected drop count at 1"
echo "${OUT2}" | grep -q '0\ pkt\ (dropped\ 30'  || die "unexpected drop count at 2"
echo "${OUT3}" | grep -q '0\ pkt\ (dropped\ 40'  || die "unexpected drop count at 3"
echo "${OUT4}" | grep -q '20\ pkt\ (dropped\ 40' || die "unexpected accept count at 4"
