#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Author: Matthias May <matthias.may@westermo.com>
#
# This script evaluates ip tunnels that are capable of carrying L2 traffic
# if they inherit or set the inheritable fields.
# Namely these tunnels are: 'gretap', 'vxlan' and 'geneve'.
# Checked inheritable fields are: TOS and TTL.
# The outer tunnel protocol of 'IPv4' or 'IPv6' is verified-
# As payload frames of type 'IPv4', 'IPv6' and 'other'(ARP) are verified.
# In addition this script also checks if forcing a specific field in the
# outer header is working.

# Return 4 by default (Kselftest SKIP code)
ERR=4

if [ "$(id -u)" != "0" ]; then
	echo "Please run as root."
	exit $ERR
fi
if ! which tcpdump > /dev/null 2>&1; then
	echo "No tcpdump found. Required for this test."
	exit $ERR
fi

expected_tos="0x00"
expected_ttl="0"
failed=false

readonly NS0=$(mktemp -u ns0-XXXXXXXX)
readonly NS1=$(mktemp -u ns1-XXXXXXXX)

RUN_NS0="ip netns exec ${NS0}"

get_random_tos() {
	# Get a random hex tos value between 0x00 and 0xfc, a multiple of 4
	echo "0x$(tr -dc '0-9a-f' < /dev/urandom | head -c 1)\
$(tr -dc '048c' < /dev/urandom | head -c 1)"
}
get_random_ttl() {
	# Get a random dec value between 0 and 255
	printf "%d" "0x$(tr -dc '0-9a-f' < /dev/urandom | head -c 2)"
}
get_field() {
	# Expects to get the 'head -n 1' of a captured frame by tcpdump.
	# Parses this first line and returns the specified field.
	local field="$1"
	local input="$2"
	local found=false
	input="$(echo "$input" | tr -d '(),')"
	for input_field in $input; do
		if $found; then
			echo "$input_field"
			return
		fi
		# The next field that we iterate over is the looked for value
		if [ "$input_field" = "$field" ]; then
			found=true
		fi
	done
	echo "0"
}
setup() {
	local type="$1"
	local outer="$2"
	local inner="$3"
	local tos_ttl="$4"
	local vlan="$5"
	local test_tos="0x00"
	local test_ttl="0"

	# We don't want a test-tos of 0x00,
	# because this is the value that we get when no tos is set.
	expected_tos="$(get_random_tos)"
	while [ "$expected_tos" = "0x00" ]; do
		expected_tos="$(get_random_tos)"
	done
	if [ "$tos_ttl" = "random" ]; then
		test_tos="$expected_tos"
		tos="fixed $test_tos"
	elif [ "$tos_ttl" = "inherit" ]; then
		test_tos="$tos_ttl"
		tos="inherit $expected_tos"
	fi

	# We don't want a test-ttl of 64 or 0,
	# because 64 is when no ttl is set and 0 is not a valid ttl.
	expected_ttl="$(get_random_ttl)"
	while [ "$expected_ttl" = "64" ] || [ "$expected_ttl" = "0" ]; do
		expected_ttl="$(get_random_ttl)"
	done

	if [ "$tos_ttl" = "random" ]; then
		test_ttl="$expected_ttl"
		ttl="fixed $test_ttl"
	elif [ "$tos_ttl" = "inherit" ]; then
		test_ttl="$tos_ttl"
		ttl="inherit $expected_ttl"
	fi
	printf "│%7s │%6s │%6s │%13s │%13s │%6s │" \
	"$type" "$outer" "$inner" "$tos" "$ttl" "$vlan"

	# Create netns NS0 and NS1 and connect them with a veth pair
	ip netns add "${NS0}"
	ip netns add "${NS1}"
	ip link add name veth0 netns "${NS0}" type veth \
		peer name veth1 netns "${NS1}"
	ip -netns "${NS0}" link set dev veth0 up
	ip -netns "${NS1}" link set dev veth1 up
	ip -netns "${NS0}" address flush dev veth0
	ip -netns "${NS1}" address flush dev veth1

	local local_addr1=""
	local local_addr2=""
	if [ "$type" = "gre" ] || [ "$type" = "vxlan" ]; then
		if [ "$outer" = "4" ]; then
			local_addr1="local 198.18.0.1"
			local_addr2="local 198.18.0.2"
		elif [ "$outer" = "6" ]; then
			local_addr1="local fdd1:ced0:5d88:3fce::1"
			local_addr2="local fdd1:ced0:5d88:3fce::2"
		fi
	fi
	local vxlan=""
	if [ "$type" = "vxlan" ]; then
		vxlan="vni 100 dstport 4789"
	fi
	local geneve=""
	if [ "$type" = "geneve" ]; then
		geneve="vni 100"
	fi
	# Create tunnel and assign outer IPv4/IPv6 addresses
	if [ "$outer" = "4" ]; then
		if [ "$type" = "gre" ]; then
			type="gretap"
		fi
		ip -netns "${NS0}" address add 198.18.0.1/24 dev veth0
		ip -netns "${NS1}" address add 198.18.0.2/24 dev veth1
		ip -netns "${NS0}" link add name tep0 type $type $local_addr1 \
			remote 198.18.0.2 tos $test_tos ttl $test_ttl         \
			$vxlan $geneve
		ip -netns "${NS1}" link add name tep1 type $type $local_addr2 \
			remote 198.18.0.1 tos $test_tos ttl $test_ttl         \
			$vxlan $geneve
	elif [ "$outer" = "6" ]; then
		if [ "$type" = "gre" ]; then
			type="ip6gretap"
		fi
		ip -netns "${NS0}" address add fdd1:ced0:5d88:3fce::1/64 \
			dev veth0 nodad
		ip -netns "${NS1}" address add fdd1:ced0:5d88:3fce::2/64 \
			dev veth1 nodad
		ip -netns "${NS0}" link add name tep0 type $type $local_addr1 \
			remote fdd1:ced0:5d88:3fce::2 tos $test_tos           \
			ttl $test_ttl $vxlan $geneve
		ip -netns "${NS1}" link add name tep1 type $type $local_addr2 \
			remote fdd1:ced0:5d88:3fce::1 tos $test_tos           \
			ttl $test_ttl $vxlan $geneve
	fi

	# Bring L2-tunnel link up and create VLAN on top
	ip -netns "${NS0}" link set tep0 up
	ip -netns "${NS1}" link set tep1 up
	ip -netns "${NS0}" address flush dev tep0
	ip -netns "${NS1}" address flush dev tep1
	local parent
	if $vlan; then
		parent="vlan99-"
		ip -netns "${NS0}" link add link tep0 name ${parent}0 \
			type vlan id 99
		ip -netns "${NS1}" link add link tep1 name ${parent}1 \
			type vlan id 99
		ip -netns "${NS0}" link set dev ${parent}0 up
		ip -netns "${NS1}" link set dev ${parent}1 up
		ip -netns "${NS0}" address flush dev ${parent}0
		ip -netns "${NS1}" address flush dev ${parent}1
	else
		parent="tep"
	fi

	# Assign inner IPv4/IPv6 addresses
	if [ "$inner" = "4" ] || [ "$inner" = "other" ]; then
		ip -netns "${NS0}" address add 198.19.0.1/24 brd + dev ${parent}0
		ip -netns "${NS1}" address add 198.19.0.2/24 brd + dev ${parent}1
	elif [ "$inner" = "6" ]; then
		ip -netns "${NS0}" address add fdd4:96cf:4eae:443b::1/64 \
			dev ${parent}0 nodad
		ip -netns "${NS1}" address add fdd4:96cf:4eae:443b::2/64 \
			dev ${parent}1 nodad
	fi
}

verify() {
	local outer="$1"
	local inner="$2"
	local tos_ttl="$3"
	local vlan="$4"

	local ping_pid out captured_tos captured_ttl result

	local ping_dst
	if [ "$inner" = "4" ]; then
		ping_dst="198.19.0.2"
	elif [ "$inner" = "6" ]; then
		ping_dst="fdd4:96cf:4eae:443b::2"
	elif [ "$inner" = "other" ]; then
		ping_dst="198.19.0.3" # Generates ARPs which are not IPv4/IPv6
	fi
	if [ "$tos_ttl" = "inherit" ]; then
		${RUN_NS0} ping -i 0.1 $ping_dst -Q "$expected_tos"          \
			 -t "$expected_ttl" 2>/dev/null 1>&2 & ping_pid="$!"
	else
		${RUN_NS0} ping -i 0.1 $ping_dst 2>/dev/null 1>&2 & ping_pid="$!"
	fi
	local tunnel_type_offset tunnel_type_proto req_proto_offset req_offset
	if [ "$type" = "gre" ]; then
		tunnel_type_proto="0x2f"
	elif [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
		tunnel_type_proto="0x11"
	fi
	if [ "$outer" = "4" ]; then
		tunnel_type_offset="9"
		if [ "$inner" = "4" ]; then
			req_proto_offset="47"
			req_offset="58"
			if [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
				req_proto_offset="$((req_proto_offset + 12))"
				req_offset="$((req_offset + 12))"
			fi
			if $vlan; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			out="$(${RUN_NS0} tcpdump --immediate-mode -p -c 1 -v \
				-i veth0 -n                                   \
				ip[$tunnel_type_offset] = $tunnel_type_proto and \
				ip[$req_proto_offset] = 0x01 and              \
				ip[$req_offset] = 0x08 2>/dev/null            \
				| head -n 1)"
		elif [ "$inner" = "6" ]; then
			req_proto_offset="44"
			req_offset="78"
			if [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
				req_proto_offset="$((req_proto_offset + 12))"
				req_offset="$((req_offset + 12))"
			fi
			if $vlan; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			out="$(${RUN_NS0} tcpdump --immediate-mode -p -c 1 -v \
				-i veth0 -n                                   \
				ip[$tunnel_type_offset] = $tunnel_type_proto and \
				ip[$req_proto_offset] = 0x3a and              \
				ip[$req_offset] = 0x80 2>/dev/null            \
				| head -n 1)"
		elif [ "$inner" = "other" ]; then
			req_proto_offset="36"
			req_offset="45"
			if [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
				req_proto_offset="$((req_proto_offset + 12))"
				req_offset="$((req_offset + 12))"
			fi
			if $vlan; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			if [ "$tos_ttl" = "inherit" ]; then
				expected_tos="0x00"
				expected_ttl="64"
			fi
			out="$(${RUN_NS0} tcpdump --immediate-mode -p -c 1 -v \
				-i veth0 -n                                   \
				ip[$tunnel_type_offset] = $tunnel_type_proto and \
				ip[$req_proto_offset] = 0x08 and              \
				ip[$((req_proto_offset + 1))] = 0x06 and      \
				ip[$req_offset] = 0x01 2>/dev/null            \
				| head -n 1)"
		fi
	elif [ "$outer" = "6" ]; then
		if [ "$type" = "gre" ]; then
			tunnel_type_offset="40"
		elif [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
			tunnel_type_offset="6"
		fi
		if [ "$inner" = "4" ]; then
			local req_proto_offset="75"
			local req_offset="86"
			if [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			if $vlan; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			out="$(${RUN_NS0} tcpdump --immediate-mode -p -c 1 -v \
				-i veth0 -n                                   \
				ip6[$tunnel_type_offset] = $tunnel_type_proto and \
				ip6[$req_proto_offset] = 0x01 and             \
				ip6[$req_offset] = 0x08 2>/dev/null           \
				| head -n 1)"
		elif [ "$inner" = "6" ]; then
			local req_proto_offset="72"
			local req_offset="106"
			if [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			if $vlan; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			out="$(${RUN_NS0} tcpdump --immediate-mode -p -c 1 -v \
				-i veth0 -n                                   \
				ip6[$tunnel_type_offset] = $tunnel_type_proto and \
				ip6[$req_proto_offset] = 0x3a and             \
				ip6[$req_offset] = 0x80 2>/dev/null           \
				| head -n 1)"
		elif [ "$inner" = "other" ]; then
			local req_proto_offset="64"
			local req_offset="73"
			if [ "$type" = "vxlan" ] || [ "$type" = "geneve" ]; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			if $vlan; then
				req_proto_offset="$((req_proto_offset + 4))"
				req_offset="$((req_offset + 4))"
			fi
			if [ "$tos_ttl" = "inherit" ]; then
				expected_tos="0x00"
				expected_ttl="64"
			fi
			out="$(${RUN_NS0} tcpdump --immediate-mode -p -c 1 -v \
				-i veth0 -n                                   \
				ip6[$tunnel_type_offset] = $tunnel_type_proto and \
				ip6[$req_proto_offset] = 0x08 and             \
				ip6[$((req_proto_offset + 1))] = 0x06 and     \
				ip6[$req_offset] = 0x01 2>/dev/null           \
				| head -n 1)"
		fi
	fi
	kill -9 $ping_pid
	wait $ping_pid 2>/dev/null || true
	result="FAIL"
	if [ "$outer" = "4" ]; then
		captured_ttl="$(get_field "ttl" "$out")"
		captured_tos="$(printf "0x%02x" "$(get_field "tos" "$out")")"
		if [ "$captured_tos" = "$expected_tos" ] &&
		   [ "$captured_ttl" = "$expected_ttl" ]; then
			result="OK"
		fi
	elif [ "$outer" = "6" ]; then
		captured_ttl="$(get_field "hlim" "$out")"
		captured_tos="$(printf "0x%02x" "$(get_field "class" "$out")")"
		if [ "$captured_tos" = "$expected_tos" ] &&
		   [ "$captured_ttl" = "$expected_ttl" ]; then
			result="OK"
		fi
	fi

	printf "%7s │\n" "$result"
	if [ "$result" = "FAIL" ]; then
		failed=true
		if [ "$captured_tos" != "$expected_tos" ]; then
			printf "│%43s%27s │\n" \
			"Expected TOS value: $expected_tos" \
			"Captured TOS value: $captured_tos"
		fi
		if [ "$captured_ttl" != "$expected_ttl" ]; then
			printf "│%43s%27s │\n" \
			"Expected TTL value: $expected_ttl" \
			"Captured TTL value: $captured_ttl"
		fi
		printf "│%71s│\n" " "
	fi
}

cleanup() {
	ip netns del "${NS0}" 2>/dev/null
	ip netns del "${NS1}" 2>/dev/null
}

exit_handler() {
	# Don't exit immediately if one of the intermediate commands fails.
	# We might be called at the end of the script, when the network
	# namespaces have already been deleted. So cleanup() may fail, but we
	# still need to run until 'exit $ERR' or the script won't return the
	# correct error code.
	set +e

	cleanup

	exit $ERR
}

# Restore the default SIGINT handler (just in case) and exit.
# The exit handler will take care of cleaning everything up.
interrupted() {
	trap - INT

	exit $ERR
}

set -e
trap exit_handler EXIT
trap interrupted INT

printf "┌────────┬───────┬───────┬──────────────┬"
printf "──────────────┬───────┬────────┐\n"
for type in gre vxlan geneve; do
	if ! $(modprobe "$type" 2>/dev/null); then
		continue
	fi
	for outer in 4 6; do
		printf "├────────┼───────┼───────┼──────────────┼"
		printf "──────────────┼───────┼────────┤\n"
		printf "│  Type  │ outer | inner │     tos      │"
		printf "      ttl     │  vlan │ result │\n"
		for inner in 4 6 other; do
			printf "├────────┼───────┼───────┼──────────────┼"
			printf "──────────────┼───────┼────────┤\n"
			for tos_ttl in inherit random; do
				for vlan in false true; do
					setup "$type" "$outer" "$inner" \
					"$tos_ttl" "$vlan"
					verify "$outer" "$inner" "$tos_ttl" \
					"$vlan"
					cleanup
				done
			done
		done
	done
done
printf "└────────┴───────┴───────┴──────────────┴"
printf "──────────────┴───────┴────────┘\n"

# All tests done.
# Set ERR appropriately: it will be returned by the exit handler.
if $failed; then
	ERR=1
else
	ERR=0
fi
