#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Test YNL CLI functionality

# Load KTAP test helpers
KSELFTEST_KTAP_HELPERS="$(dirname "$(realpath "$0")")/../../../testing/selftests/kselftest/ktap_helpers.sh"
# shellcheck source=../../../testing/selftests/kselftest/ktap_helpers.sh
source "$KSELFTEST_KTAP_HELPERS"

# Default ynl path for direct execution, can be overridden by make install
ynl="../pyynl/cli.py"

readonly NSIM_ID="1338"
readonly NSIM_DEV_NAME="nsim${NSIM_ID}"
readonly VETH_A="veth_a"
readonly VETH_B="veth_b"

testns="ynl-$(mktemp -u XXXXXX)"
TESTS_NO=0

# Test listing available families
cli_list_families()
{
	if $ynl --list-families &>/dev/null; then
		ktap_test_pass "YNL CLI list families"
	else
		ktap_test_fail "YNL CLI list families"
	fi
}
TESTS_NO=$((TESTS_NO + 1))

# Test netdev family operations (dev-get, queue-get)
cli_netdev_ops()
{
	local dev_output
	local ifindex

	ifindex=$(ip netns exec "$testns" cat /sys/class/net/"$NSIM_DEV_NAME"/ifindex 2>/dev/null)

	dev_output=$(ip netns exec "$testns" $ynl --family netdev \
		--do dev-get --json "{\"ifindex\": $ifindex}" 2>/dev/null)

	if ! echo "$dev_output" | grep -q "ifindex"; then
		ktap_test_fail "YNL CLI netdev operations (netdev dev-get output missing ifindex)"
		return
	fi

	if ! ip netns exec "$testns" $ynl --family netdev \
		--dump queue-get --json "{\"ifindex\": $ifindex}" &>/dev/null; then
		ktap_test_fail "YNL CLI netdev operations (failed to get netdev queue info)"
		return
	fi

	ktap_test_pass "YNL CLI netdev operations"
}
TESTS_NO=$((TESTS_NO + 1))

# Test ethtool family operations (rings-get, linkinfo-get)
cli_ethtool_ops()
{
	local rings_output
	local linkinfo_output

	rings_output=$(ip netns exec "$testns" $ynl --family ethtool \
		--do rings-get --json "{\"header\": {\"dev-name\": \"$NSIM_DEV_NAME\"}}" 2>/dev/null)

	if ! echo "$rings_output" | grep -q "header"; then
		ktap_test_fail "YNL CLI ethtool operations (ethtool rings-get output missing header)"
		return
	fi

	linkinfo_output=$(ip netns exec "$testns" $ynl --family ethtool \
		--do linkinfo-get --json "{\"header\": {\"dev-name\": \"$VETH_A\"}}" 2>/dev/null)

	if ! echo "$linkinfo_output" | grep -q "header"; then
		ktap_test_fail "YNL CLI ethtool operations (ethtool linkinfo-get output missing header)"
		return
	fi

	ktap_test_pass "YNL CLI ethtool operations"
}
TESTS_NO=$((TESTS_NO + 1))

# Test rt-route family operations
cli_rt_route_ops()
{
	local ifindex

	if ! $ynl --list-families 2>/dev/null | grep -q "rt-route"; then
		ktap_test_skip "YNL CLI rt-route operations (rt-route family not available)"
		return
	fi

	ifindex=$(ip netns exec "$testns" cat /sys/class/net/"$NSIM_DEV_NAME"/ifindex 2>/dev/null)

	# Add route: 192.0.2.0/24 dev $dev scope link
	if ! ip netns exec "$testns" $ynl --family rt-route --do newroute --create \
		--json "{\"dst\": \"192.0.2.0\", \"oif\": $ifindex, \"rtm-dst-len\": 24, \"rtm-family\": 2, \"rtm-scope\": 253, \"rtm-type\": 1, \"rtm-protocol\": 3, \"rtm-table\": 254}" &>/dev/null; then
		ktap_test_fail "YNL CLI rt-route operations (failed to add route)"
		return
	fi

	local route_output
	route_output=$(ip netns exec "$testns" $ynl --family rt-route --dump getroute 2>/dev/null)
	if echo "$route_output" | grep -q "192.0.2.0"; then
		ktap_test_pass "YNL CLI rt-route operations"
	else
		ktap_test_fail "YNL CLI rt-route operations (failed to verify route)"
	fi

	ip netns exec "$testns" $ynl --family rt-route --do delroute \
		--json "{\"dst\": \"192.0.2.0\", \"oif\": $ifindex, \"rtm-dst-len\": 24, \"rtm-family\": 2, \"rtm-scope\": 253, \"rtm-type\": 1, \"rtm-protocol\": 3, \"rtm-table\": 254}" &>/dev/null
}
TESTS_NO=$((TESTS_NO + 1))

# Test rt-addr family operations
cli_rt_addr_ops()
{
	local ifindex

	if ! $ynl --list-families 2>/dev/null | grep -q "rt-addr"; then
		ktap_test_skip "YNL CLI rt-addr operations (rt-addr family not available)"
		return
	fi

	ifindex=$(ip netns exec "$testns" cat /sys/class/net/"$NSIM_DEV_NAME"/ifindex 2>/dev/null)

	if ! ip netns exec "$testns" $ynl --family rt-addr --do newaddr \
		--json "{\"ifa-index\": $ifindex, \"local\": \"192.0.2.100\", \"ifa-prefixlen\": 24, \"ifa-family\": 2}" &>/dev/null; then
		ktap_test_fail "YNL CLI rt-addr operations (failed to add address)"
		return
	fi

	local addr_output
	addr_output=$(ip netns exec "$testns" $ynl --family rt-addr --dump getaddr 2>/dev/null)
	if echo "$addr_output" | grep -q "192.0.2.100"; then
		ktap_test_pass "YNL CLI rt-addr operations"
	else
		ktap_test_fail "YNL CLI rt-addr operations (failed to verify address)"
	fi

	ip netns exec "$testns" $ynl --family rt-addr --do deladdr \
		--json "{\"ifa-index\": $ifindex, \"local\": \"192.0.2.100\", \"ifa-prefixlen\": 24, \"ifa-family\": 2}" &>/dev/null
}
TESTS_NO=$((TESTS_NO + 1))

# Test rt-link family operations
cli_rt_link_ops()
{
	if ! $ynl --list-families 2>/dev/null | grep -q "rt-link"; then
		ktap_test_skip "YNL CLI rt-link operations (rt-link family not available)"
		return
	fi

	if ! ip netns exec "$testns" $ynl --family rt-link --do newlink --create \
		--json "{\"ifname\": \"dummy0\", \"linkinfo\": {\"kind\": \"dummy\"}}" &>/dev/null; then
		ktap_test_fail "YNL CLI rt-link operations (failed to add link)"
		return
	fi

	local link_output
	link_output=$(ip netns exec "$testns" $ynl --family rt-link --dump getlink 2>/dev/null)
	if echo "$link_output" | grep -q "$NSIM_DEV_NAME" && echo "$link_output" | grep -q "dummy0"; then
		ktap_test_pass "YNL CLI rt-link operations"
	else
		ktap_test_fail "YNL CLI rt-link operations (failed to verify link)"
	fi

	ip netns exec "$testns" $ynl --family rt-link --do dellink \
		--json "{\"ifname\": \"dummy0\"}" &>/dev/null
}
TESTS_NO=$((TESTS_NO + 1))

# Test rt-neigh family operations
cli_rt_neigh_ops()
{
	local ifindex

	if ! $ynl --list-families 2>/dev/null | grep -q "rt-neigh"; then
		ktap_test_skip "YNL CLI rt-neigh operations (rt-neigh family not available)"
		return
	fi

	ifindex=$(ip netns exec "$testns" cat /sys/class/net/"$NSIM_DEV_NAME"/ifindex 2>/dev/null)

	# Add neighbor: 192.0.2.1 dev nsim1338 lladdr 11:22:33:44:55:66 PERMANENT
	if ! ip netns exec "$testns" $ynl --family rt-neigh --do newneigh --create \
		--json "{\"ndm-ifindex\": $ifindex, \"dst\": \"192.0.2.1\", \"lladdr\": \"11:22:33:44:55:66\", \"ndm-family\": 2, \"ndm-state\": 128}" &>/dev/null; then
		ktap_test_fail "YNL CLI rt-neigh operations (failed to add neighbor)"
	fi

	local neigh_output
	neigh_output=$(ip netns exec "$testns" $ynl --family rt-neigh --dump getneigh 2>/dev/null)
	if echo "$neigh_output" | grep -q "192.0.2.1"; then
		ktap_test_pass "YNL CLI rt-neigh operations"
	else
		ktap_test_fail "YNL CLI rt-neigh operations (failed to verify neighbor)"
	fi

	ip netns exec "$testns" $ynl --family rt-neigh --do delneigh \
		--json "{\"ndm-ifindex\": $ifindex, \"dst\": \"192.0.2.1\", \"lladdr\": \"11:22:33:44:55:66\", \"ndm-family\": 2}" &>/dev/null
}
TESTS_NO=$((TESTS_NO + 1))

# Test rt-rule family operations
cli_rt_rule_ops()
{
	if ! $ynl --list-families 2>/dev/null | grep -q "rt-rule"; then
		ktap_test_skip "YNL CLI rt-rule operations (rt-rule family not available)"
		return
	fi

	# Add rule: from 192.0.2.0/24 lookup 100 none
	if ! ip netns exec "$testns" $ynl --family rt-rule --do newrule \
		--json "{\"family\": 2, \"src-len\": 24, \"src\": \"192.0.2.0\", \"table\": 100}" &>/dev/null; then
		ktap_test_fail "YNL CLI rt-rule operations (failed to add rule)"
		return
	fi

	local rule_output
	rule_output=$(ip netns exec "$testns" $ynl --family rt-rule --dump getrule 2>/dev/null)
	if echo "$rule_output" | grep -q "192.0.2.0"; then
		ktap_test_pass "YNL CLI rt-rule operations"
	else
		ktap_test_fail "YNL CLI rt-rule operations (failed to verify rule)"
	fi

	ip netns exec "$testns" $ynl --family rt-rule --do delrule \
		--json "{\"family\": 2, \"src-len\": 24, \"src\": \"192.0.2.0\", \"table\": 100}" &>/dev/null
}
TESTS_NO=$((TESTS_NO + 1))

# Test nlctrl family operations
cli_nlctrl_ops()
{
	local family_output

	if ! family_output=$($ynl --family nlctrl \
		--do getfamily --json "{\"family-name\": \"netdev\"}" 2>/dev/null); then
		ktap_test_fail "YNL CLI nlctrl getfamily (failed to get nlctrl family info)"
		return
	fi

	if ! echo "$family_output" | grep -q "family-name"; then
		ktap_test_fail "YNL CLI nlctrl getfamily (nlctrl getfamily output missing family-name)"
		return
	fi

	if ! echo "$family_output" | grep -q "family-id"; then
		ktap_test_fail "YNL CLI nlctrl getfamily (nlctrl getfamily output missing family-id)"
		return
	fi

	ktap_test_pass "YNL CLI nlctrl getfamily"
}
TESTS_NO=$((TESTS_NO + 1))

setup()
{
	modprobe netdevsim &> /dev/null
	if ! [ -f /sys/bus/netdevsim/new_device ]; then
		ktap_skip_all "netdevsim module not available"
		exit "$KSFT_SKIP"
	fi

	if ! ip netns add "$testns" 2>/dev/null; then
		ktap_skip_all "failed to create test namespace"
		exit "$KSFT_SKIP"
	fi

	echo "$NSIM_ID 1" | ip netns exec "$testns" tee /sys/bus/netdevsim/new_device >/dev/null 2>&1 || {
		ktap_skip_all "failed to create netdevsim device"
		exit "$KSFT_SKIP"
	}

	local dev
	dev=$(ip netns exec "$testns" ls /sys/bus/netdevsim/devices/netdevsim$NSIM_ID/net 2>/dev/null | head -1)
	if [[ -z "$dev" ]]; then
		ktap_skip_all "failed to find netdevsim device"
		exit "$KSFT_SKIP"
	fi

	ip -netns "$testns" link set dev "$dev" name "$NSIM_DEV_NAME" 2>/dev/null || {
		ktap_skip_all "failed to rename netdevsim device"
		exit "$KSFT_SKIP"
	}

	ip -netns "$testns" link set dev "$NSIM_DEV_NAME" up 2>/dev/null

	if ! ip -n "$testns" link add "$VETH_A" type veth peer name "$VETH_B" 2>/dev/null; then
		ktap_skip_all "failed to create veth pair"
		exit "$KSFT_SKIP"
	fi

	ip -n "$testns" link set "$VETH_A" up 2>/dev/null
	ip -n "$testns" link set "$VETH_B" up 2>/dev/null
}

cleanup()
{
	ip netns exec "$testns" bash -c "echo $NSIM_ID > /sys/bus/netdevsim/del_device" 2>/dev/null || true
	ip netns del "$testns" 2>/dev/null || true
}

# Check if ynl command is available
if ! command -v $ynl &>/dev/null && [[ ! -x $ynl ]]; then
	ktap_skip_all "ynl command not found: $ynl"
	exit "$KSFT_SKIP"
fi

trap cleanup EXIT

ktap_print_header
setup
ktap_set_plan "${TESTS_NO}"

cli_list_families
cli_netdev_ops
cli_ethtool_ops
cli_rt_route_ops
cli_rt_addr_ops
cli_rt_link_ops
cli_rt_neigh_ops
cli_rt_rule_ops
cli_nlctrl_ops

ktap_finished
