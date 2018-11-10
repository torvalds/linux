#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Load BPF flow dissector and verify it correctly dissects traffic
export TESTNAME=test_flow_dissector
unmount=0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

msg="skip all tests:"
if [ $UID != 0 ]; then
	echo $msg please run this as root >&2
	exit $ksft_skip
fi

# This test needs to be run in a network namespace with in_netns.sh. Check if
# this is the case and run it with in_netns.sh if it is being run in the root
# namespace.
if [[ -z $(ip netns identify $$) ]]; then
	../net/in_netns.sh "$0" "$@"
	exit $?
fi

# Determine selftest success via shell exit code
exit_handler()
{
	if (( $? == 0 )); then
		echo "selftests: $TESTNAME [PASS]";
	else
		echo "selftests: $TESTNAME [FAILED]";
	fi

	set +e

	# Cleanup
	tc filter del dev lo ingress pref 1337 2> /dev/null
	tc qdisc del dev lo ingress 2> /dev/null
	./flow_dissector_load -d 2> /dev/null
	if [ $unmount -ne 0 ]; then
		umount bpffs 2> /dev/null
	fi
}

# Exit script immediately (well catched by trap handler) if any
# program/thing exits with a non-zero status.
set -e

# (Use 'trap -l' to list meaning of numbers)
trap exit_handler 0 2 3 6 9

# Mount BPF file system
if /bin/mount | grep /sys/fs/bpf > /dev/null; then
	echo "bpffs already mounted"
else
	echo "bpffs not mounted. Mounting..."
	unmount=1
	/bin/mount bpffs /sys/fs/bpf -t bpf
fi

# Attach BPF program
./flow_dissector_load -p bpf_flow.o -s flow_dissector

# Setup
tc qdisc add dev lo ingress

echo "Testing IPv4..."
# Drops all IP/UDP packets coming from port 9
tc filter add dev lo parent ffff: protocol ip pref 1337 flower ip_proto \
	udp src_port 9 action drop

# Send 10 IPv4/UDP packets from port 8. Filter should not drop any.
./test_flow_dissector -i 4 -f 8
# Send 10 IPv4/UDP packets from port 9. Filter should drop all.
./test_flow_dissector -i 4 -f 9 -F
# Send 10 IPv4/UDP packets from port 10. Filter should not drop any.
./test_flow_dissector -i 4 -f 10

echo "Testing IPIP..."
# Send 10 IPv4/IPv4/UDP packets from port 8. Filter should not drop any.
./with_addr.sh ./with_tunnels.sh ./test_flow_dissector -o 4 -e bare -i 4 \
	-D 192.168.0.1 -S 1.1.1.1 -f 8
# Send 10 IPv4/IPv4/UDP packets from port 9. Filter should drop all.
./with_addr.sh ./with_tunnels.sh ./test_flow_dissector -o 4 -e bare -i 4 \
	-D 192.168.0.1 -S 1.1.1.1 -f 9 -F
# Send 10 IPv4/IPv4/UDP packets from port 10. Filter should not drop any.
./with_addr.sh ./with_tunnels.sh ./test_flow_dissector -o 4 -e bare -i 4 \
	-D 192.168.0.1 -S 1.1.1.1 -f 10

echo "Testing IPv4 + GRE..."
# Send 10 IPv4/GRE/IPv4/UDP packets from port 8. Filter should not drop any.
./with_addr.sh ./with_tunnels.sh ./test_flow_dissector -o 4 -e gre -i 4 \
	-D 192.168.0.1 -S 1.1.1.1 -f 8
# Send 10 IPv4/GRE/IPv4/UDP packets from port 9. Filter should drop all.
./with_addr.sh ./with_tunnels.sh ./test_flow_dissector -o 4 -e gre -i 4 \
	-D 192.168.0.1 -S 1.1.1.1 -f 9 -F
# Send 10 IPv4/GRE/IPv4/UDP packets from port 10. Filter should not drop any.
./with_addr.sh ./with_tunnels.sh ./test_flow_dissector -o 4 -e gre -i 4 \
	-D 192.168.0.1 -S 1.1.1.1 -f 10

tc filter del dev lo ingress pref 1337

echo "Testing IPv6..."
# Drops all IPv6/UDP packets coming from port 9
tc filter add dev lo parent ffff: protocol ipv6 pref 1337 flower ip_proto \
	udp src_port 9 action drop

# Send 10 IPv6/UDP packets from port 8. Filter should not drop any.
./test_flow_dissector -i 6 -f 8
# Send 10 IPv6/UDP packets from port 9. Filter should drop all.
./test_flow_dissector -i 6 -f 9 -F
# Send 10 IPv6/UDP packets from port 10. Filter should not drop any.
./test_flow_dissector -i 6 -f 10

exit 0
