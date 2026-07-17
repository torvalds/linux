#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test a PRP RedBox (PRP-SAN): a SAN that sits behind the interlink port must
# reach, and be reached by, a peer DANP on the PRP network with its own MAC
# preserved on the wire, and the RedBox must announce the SAN with a RedBox-MAC
# TLV (terminated by an EOT marker) in its PRP supervision frames.
#
#   RB    PRP RedBox: prp0 over rb_a/rb_b (LAN A/B) + interlink rb_il
#   PEER  peer DANP : prp0 over pe_a/pe_b, 100.64.0.2
#   SAN   SAN       : san_il, own MAC, 100.64.0.51 (behind the interlink)

ipv6=false

source ./hsr_common.sh

check_prerequisites

if ! command -v tcpdump >/dev/null 2>&1; then
	echo "SKIP: This test requires tcpdump"
	exit $ksft_skip
fi

if ! ip link help hsr 2>&1 | grep -q interlink; then
	echo "SKIP: iproute2 too old (no hsr interlink support)"
	exit $ksft_skip
fi

setup_ns RB PEER SAN
trap 'cleanup_ns "$RB" "$PEER" "$SAN"' EXIT

ip link add rb_a netns "$RB" type veth peer name pe_a netns "$PEER"
ip link add rb_b netns "$RB" type veth peer name pe_b netns "$PEER"
ip link add rb_il netns "$RB" type veth peer name san_il netns "$SAN"

ip -n "$RB"   link set rb_a up
ip -n "$RB"   link set rb_b up
ip -n "$RB"   link set rb_il up
ip -n "$PEER" link set pe_a up
ip -n "$PEER" link set pe_b up
ip -n "$SAN"  link set san_il up
ip -n "$SAN"  addr add 100.64.0.51/24 dev san_il

# Feature gate: PRP interlink (RedBox) creation. A kernel without PRP RedBox
# support rejects this with -EINVAL, so SKIP rather than FAIL.
if ! ip -n "$RB" link add name prp0 type hsr slave1 rb_a slave2 rb_b \
     interlink rb_il proto 1 2>/dev/null; then
	echo "SKIP: kernel without PRP RedBox (interlink) support"
	exit $ksft_skip
fi
ip -n "$RB"   link set prp0 up
ip -n "$PEER" link add name prp0 type hsr slave1 pe_a slave2 pe_b proto 1
ip -n "$PEER" link set prp0 up
ip -n "$PEER" addr add 100.64.0.2/24 dev prp0
sleep 1

san_mac=$(ip -n "$SAN" -br link show san_il | awk '{print $3}')
rb_mac=$(ip -n "$RB" -br link show rb_il | awk '{print $3}')

# Bidirectional unicast across the interlink.
do_ping "$PEER" 100.64.0.51
do_ping "$SAN"  100.64.0.2
stop_if_error "PRP RedBox bidirectional unicast failed"

# The SAN source MAC must be preserved on the PRP network, not laundered to the
# RedBox MAC: the peer resolves the SAN IP to the SAN's own MAC.
neigh=$(ip -n "$PEER" neigh show 100.64.0.51 | awk '{print $5}')
if [ "$neigh" != "$san_mac" ]; then
	echo "SAN MAC preservation [ FAIL ]: peer resolved 100.64.0.51 to" \
	     "'$neigh', expected $san_mac" 1>&2
	ret=1
fi
stop_if_error "SAN MAC not preserved on the PRP network"

# The proxy-announce supervision frame must carry, in order, the life-check TLV
# (type 0x14, len 6) + MacAddressA == SAN MAC + the RedBox-MAC TLV (type 0x1e,
# len 6) + MacAddressRedBox == RedBox MAC + the EOT marker (0x0000).
ip netns exec "$SAN" ping -i 0.2 -q 100.64.0.2 >/dev/null 2>&1 &
ping_pid=$!
cap=$(ip netns exec "$PEER" timeout 5 tcpdump -i pe_a -nn -x \
	"ether proto 0x88fb and ether src $rb_mac" 2>/dev/null || true)
kill "$ping_pid" 2>/dev/null || true
wait "$ping_pid" 2>/dev/null || true

san_hex=$(echo "$san_mac" | tr -d ':')
rb_hex=$(echo "$rb_mac" | tr -d ':')
# Reassemble contiguous frame hex: drop the "0x0010:" offset labels and spaces.
frame_hex=$(echo "$cap" | awk '/^[[:space:]]*0x[0-9a-f]+:/ {
	sub(/^[[:space:]]*0x[0-9a-f]+:[[:space:]]*/, "");
	gsub(/ /, ""); printf "%s", $0 }')
if ! echo "$frame_hex" | grep -q "1406${san_hex}1e06${rb_hex}0000"; then
	echo "supervision RedBox-MAC TLV [ FAIL ]: missing SAN MAC, Type-30" \
	     "payload, or EOT" 1>&2
	ret=1
fi
stop_if_error "PRP RedBox supervision RedBox-MAC TLV/EOT check failed"

echo "INFO: PRP RedBox (PRP-SAN) conformance checks passed"
exit $ret
