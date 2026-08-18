#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

checktool "nft --version" "run test without nft tool"
modprobe -q netdevsim

sysfs="/sys/kernel/debug/fail_function"
failname="/proc/self/make-it-fail"
duration=30
fault=0
ret=0
file_ft=""
file_rs=""
id=$((RANDOM%65536))

read -r t < /proc/sys/kernel/tainted
if [ "$t" -ne 0 ];then
	echo SKIP: kernel is tainted
	exit $ksft_skip
fi

cleanup() {
    cleanup_netdevsim "$id" "$NS"
    cleanup_ns "$NS"
    [ "$fault" -eq 1 ] && echo '!nsim_setup_tc' > "$sysfs/inject"
    rm -f "$file_ft" "$file_rs"
}
trap cleanup EXIT

skip() {
	echo "SKIP: $*"
	[ $ret -eq 0 ] && exit 4

	exit $ret
}

set -e
setup_ns NS

create_netdevsim "$id" "$NS" >/dev/null
nsim_port=$(create_netdevsim_port "$id" "$NS" 2)

file_ft=$(mktemp)
cat > "$file_ft" <<EOF
flush ruleset
table inet t {
	flowtable f {
		flags offload
		hook ingress priority filter + 10
		devices = { "$nsim_port", "dummyf1" }
	}

	chain cf {
		type filter hook forward priority 0; policy accept;
		ct state new meta l4proto tcp flow add @f
	}
}
EOF

if ip netns exec "$NS" nft -f "$file_ft"; then
	echo "PASS: flowtable offload"
else
	echo "FAIL: flowtable offload"
	ret=1
fi

file_rs=$(mktemp)
cat > "$file_rs" <<EOF
table netdev t {
	chain c {
		type filter hook ingress device $nsim_port priority 1
		flags offload
		ip saddr 10.2.1.1 ip daddr 10.2.1.2 ip protocol icmp accept
		ip saddr 10.2.1.1 ip daddr 10.2.1.3 ip protocol icmp drop
		ip saddr 10.2.1.0/24 ip daddr 10.2.1.0/24 ip protocol icmp accept
		ip6 saddr dead:beef::1 ip6 daddr dead:beef::2 meta l4proto ipv6-icmp accept
		ip6 saddr dead:beef::1 ip6 daddr dead:beef::3 meta l4proto ipv6-icmp drop
		ip6 saddr dead:beef::/64 ip6 daddr dead:beef::/64 meta l4proto ipv6-icmp accept
	}
}
EOF
if ip netns exec "$NS" nft -f "$file_rs"; then
	echo "PASS: ruleset offload"
else
	echo "FAIL: ruleset offload"
	ret=1
fi

test -d "$sysfs" || skip "$sysfs not present"
grep -q nsim_setup_tc "$sysfs/injectable" || skip "nsim_setup_tc fault injection not available"

echo Y > "$sysfs/task-filter"
echo 0 > "$sysfs/verbose"
echo "nsim_setup_tc" > "$sysfs/inject"
fault=1

p=$(((RANDOM%90) + 10))
echo $p > "$sysfs/probability"
echo -1 > "$sysfs/times"

count=0
ok=0

now=$(date +%s)
stop=$((now+duration))

# fault-injection enabled rule loads are expected to fail.
set +e
while [ "$now" -le "$stop" ]; do
	for f in "$file_ft" "$file_rs"; do
		if ip netns exec "$NS" bash -c "echo 1 > $failname ; ip netns exec \"$NS\" nft -f $f" 2> /dev/null;then
			ok=$((ok+1))
		fi
		count=$((count+1))
	done
	now=$(date +%s)
done

sleep 5

read -r t < /proc/sys/kernel/tainted
if [ "$t" -eq 0 ];then
	echo "PASS: Not tainted. $count rounds, $ok successful ruleset loads with P $p."
else
	echo "ERROR: Tainted. $count rounds, $ok successful ruleset loads with P $p."
	dmesg
	ret=1
fi

exit $ret
