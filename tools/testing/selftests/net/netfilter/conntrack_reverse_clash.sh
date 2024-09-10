#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source lib.sh

cleanup()
{
	cleanup_all_ns
}

checktool "nft --version" "run test without nft"
checktool "conntrack --version" "run test without conntrack"

trap cleanup EXIT

setup_ns ns0

# make loopback connections get nat null bindings assigned
ip netns exec "$ns0" nft -f - <<EOF
table ip nat {
        chain POSTROUTING {
                type nat hook postrouting priority srcnat; policy accept;
                oifname "nomatch" counter packets 0 bytes 0 masquerade
        }
}
EOF

do_flush()
{
	local end
	local now

	now=$(date +%s)
	end=$((now + 5))

	while [ $now -lt $end ];do
		ip netns exec "$ns0" conntrack -F 2>/dev/null
		now=$(date +%s)
	done
}

do_flush &

if ip netns exec "$ns0" ./conntrack_reverse_clash; then
	echo "PASS: No SNAT performed for null bindings"
else
	echo "ERROR: SNAT performed without any matching snat rule"
	exit 1
fi

exit 0
