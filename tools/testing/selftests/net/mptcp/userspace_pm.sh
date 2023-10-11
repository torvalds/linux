#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it.
#shellcheck disable=SC2086

# Some variables are used below but indirectly, see check_expected_one()
#shellcheck disable=SC2034

. "$(dirname "${0}")/mptcp_lib.sh"

mptcp_lib_check_mptcp
mptcp_lib_check_kallsyms

if ! mptcp_lib_has_file '/proc/sys/net/mptcp/pm_type'; then
	echo "userspace pm tests are not supported by the kernel: SKIP"
	exit ${KSFT_SKIP}
fi

if ! ip -Version &> /dev/null; then
	echo "SKIP: Cannot not run test without ip tool"
	exit ${KSFT_SKIP}
fi

ANNOUNCED=6        # MPTCP_EVENT_ANNOUNCED
REMOVED=7          # MPTCP_EVENT_REMOVED
SUB_ESTABLISHED=10 # MPTCP_EVENT_SUB_ESTABLISHED
SUB_CLOSED=11      # MPTCP_EVENT_SUB_CLOSED
LISTENER_CREATED=15 #MPTCP_EVENT_LISTENER_CREATED
LISTENER_CLOSED=16  #MPTCP_EVENT_LISTENER_CLOSED

AF_INET=2
AF_INET6=10

file=""
server_evts=""
client_evts=""
server_evts_pid=0
client_evts_pid=0
client4_pid=0
server4_pid=0
client6_pid=0
server6_pid=0
client4_token=""
server4_token=""
client6_token=""
server6_token=""
client4_port=0;
client6_port=0;
app4_port=50002
new4_port=50003
app6_port=50004
client_addr_id=${RANDOM:0:2}
server_addr_id=${RANDOM:0:2}

sec=$(date +%s)
rndh=$(printf %x "$sec")-$(mktemp -u XXXXXX)
ns1="ns1-$rndh"
ns2="ns2-$rndh"
ret=0
test_name=""

_printf() {
	stdbuf -o0 -e0 printf "${@}"
}

print_title()
{
	_printf "INFO: %s\n" "${1}"
}

# $1: test name
print_test()
{
	test_name="${1}"

	_printf "%-63s" "${test_name}"
}

print_results()
{
	_printf "[%s]\n" "${1}"
}

test_pass()
{
	print_results " OK "
	mptcp_lib_result_pass "${test_name}"
}

test_skip()
{
	print_results "SKIP"
	mptcp_lib_result_skip "${test_name}"
}

# $1: msg
test_fail()
{
	print_results "FAIL"
	ret=1

	if [ -n "${1}" ]; then
		_printf "\t%s\n" "${1}"
	fi

	mptcp_lib_result_fail "${test_name}"
}

kill_wait()
{
	[ $1 -eq 0 ] && return 0

	kill -SIGUSR1 $1 > /dev/null 2>&1
	kill $1 > /dev/null 2>&1
	wait $1 2>/dev/null
}

# This function is used in the cleanup trap
#shellcheck disable=SC2317
cleanup()
{
	print_title "Cleanup"

	# Terminate the MPTCP connection and related processes
	local pid
	for pid in $client4_pid $server4_pid $client6_pid $server6_pid\
		   $server_evts_pid $client_evts_pid
	do
		kill_wait $pid
	done

	local netns
	for netns in "$ns1" "$ns2" ;do
		ip netns del "$netns"
	done

	rm -rf $file $client_evts $server_evts

	_printf "Done\n"
}

trap cleanup EXIT

# Create and configure network namespaces for testing
for i in "$ns1" "$ns2" ;do
	ip netns add "$i" || exit 1
	ip -net "$i" link set lo up
	ip netns exec "$i" sysctl -q net.mptcp.enabled=1
	ip netns exec "$i" sysctl -q net.mptcp.pm_type=1
done

#  "$ns1"              ns2
#     ns1eth2    ns2eth1

ip link add ns1eth2 netns "$ns1" type veth peer name ns2eth1 netns "$ns2"

# Add IPv4/v6 addresses to the namespaces
ip -net "$ns1" addr add 10.0.1.1/24 dev ns1eth2
ip -net "$ns1" addr add 10.0.2.1/24 dev ns1eth2
ip -net "$ns1" addr add dead:beef:1::1/64 dev ns1eth2 nodad
ip -net "$ns1" addr add dead:beef:2::1/64 dev ns1eth2 nodad
ip -net "$ns1" link set ns1eth2 up

ip -net "$ns2" addr add 10.0.1.2/24 dev ns2eth1
ip -net "$ns2" addr add 10.0.2.2/24 dev ns2eth1
ip -net "$ns2" addr add dead:beef:1::2/64 dev ns2eth1 nodad
ip -net "$ns2" addr add dead:beef:2::2/64 dev ns2eth1 nodad
ip -net "$ns2" link set ns2eth1 up

print_title "Init"
print_test "Created network namespaces ns1, ns2"
test_pass

make_file()
{
	# Store a chunk of data in a file to transmit over an MPTCP connection
	local name=$1
	local ksize=1

	dd if=/dev/urandom of="$name" bs=2 count=$ksize 2> /dev/null
	echo -e "\nMPTCP_TEST_FILE_END_MARKER" >> "$name"
}

make_connection()
{
	if [ -z "$file" ]; then
		file=$(mktemp)
	fi
	make_file "$file" "client"

	local is_v6=$1
	local app_port=$app4_port
	local connect_addr="10.0.1.1"
	local listen_addr="0.0.0.0"
	if [ "$is_v6" = "v6" ]
	then
		connect_addr="dead:beef:1::1"
		listen_addr="::"
		app_port=$app6_port
	else
		is_v6="v4"
	fi

	# Capture netlink events over the two network namespaces running
	# the MPTCP client and server
	if [ -z "$client_evts" ]; then
		client_evts=$(mktemp)
	fi
	:>"$client_evts"
	if [ $client_evts_pid -ne 0 ]; then
		kill_wait $client_evts_pid
	fi
	ip netns exec "$ns2" ./pm_nl_ctl events >> "$client_evts" 2>&1 &
	client_evts_pid=$!
	if [ -z "$server_evts" ]; then
		server_evts=$(mktemp)
	fi
	:>"$server_evts"
	if [ $server_evts_pid -ne 0 ]; then
		kill_wait $server_evts_pid
	fi
	ip netns exec "$ns1" ./pm_nl_ctl events >> "$server_evts" 2>&1 &
	server_evts_pid=$!
	sleep 0.5

	# Run the server
	ip netns exec "$ns1" \
	   ./mptcp_connect -s MPTCP -w 300 -p $app_port -l $listen_addr > /dev/null 2>&1 &
	local server_pid=$!
	sleep 0.5

	# Run the client, transfer $file and stay connected to the server
	# to conduct tests
	ip netns exec "$ns2" \
	   ./mptcp_connect -s MPTCP -w 300 -m sendfile -p $app_port $connect_addr\
	   2>&1 > /dev/null < "$file" &
	local client_pid=$!
	sleep 1

	# Capture client/server attributes from MPTCP connection netlink events

	local client_token
	local client_port
	local client_serverside
	local server_token
	local server_serverside

	client_token=$(sed --unbuffered -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q' "$client_evts")
	client_port=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$client_evts")
	client_serverside=$(sed --unbuffered -n 's/.*\(server_side:\)\([[:digit:]]*\).*$/\2/p;q'\
				      "$client_evts")
	server_token=$(grep "type:1," "$server_evts" |
		       sed --unbuffered -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q')
	server_serverside=$(grep "type:1," "$server_evts" |
			    sed --unbuffered -n 's/.*\(server_side:\)\([[:digit:]]*\).*$/\2/p;q')

	print_test "Established IP${is_v6} MPTCP Connection ns2 => ns1"
	if [ "$client_token" != "" ] && [ "$server_token" != "" ] && [ "$client_serverside" = 0 ] &&
		   [ "$server_serverside" = 1 ]
	then
		test_pass
	else
		test_fail "Expected tokens (c:${client_token} - s:${server_token}) and server (c:${client_serverside} - s:${server_serverside})"
		mptcp_lib_result_print_all_tap
		exit 1
	fi

	if [ "$is_v6" = "v6" ]
	then
		client6_token=$client_token
		server6_token=$server_token
		client6_port=$client_port
		client6_pid=$client_pid
		server6_pid=$server_pid
	else
		client4_token=$client_token
		server4_token=$server_token
		client4_port=$client_port
		client4_pid=$client_pid
		server4_pid=$server_pid
	fi
}

# $1: var name ; $2: prev ret
check_expected_one()
{
	local var="${1}"
	local exp="e_${var}"
	local prev_ret="${2}"

	if [ "${!var}" = "${!exp}" ]
	then
		return 0
	fi

	if [ "${prev_ret}" = "0" ]
	then
		test_fail
	fi

	_printf "\tExpected value for '%s': '%s', got '%s'.\n" \
		"${var}" "${!exp}" "${!var}"
	return 1
}

# $@: all var names to check
check_expected()
{
	local rc=0
	local var

	for var in "${@}"
	do
		check_expected_one "${var}" "${rc}" || rc=1
	done

	if [ ${rc} -eq 0 ]
	then
		test_pass
		return 0
	fi

	return 1
}

verify_announce_event()
{
	local evt=$1
	local e_type=$2
	local e_token=$3
	local e_addr=$4
	local e_id=$5
	local e_dport=$6
	local e_af=$7
	local type
	local token
	local addr
	local dport
	local id

	type=$(sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	token=$(sed --unbuffered -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	if [ "$e_af" = "v6" ]
	then
		addr=$(sed --unbuffered -n 's/.*\(daddr6:\)\([0-9a-f:.]*\).*$/\2/p;q' "$evt")
	else
		addr=$(sed --unbuffered -n 's/.*\(daddr4:\)\([0-9.]*\).*$/\2/p;q' "$evt")
	fi
	dport=$(sed --unbuffered -n 's/.*\(dport:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	id=$(sed --unbuffered -n 's/.*\(rem_id:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")

	check_expected "type" "token" "addr" "dport" "id"
}

test_announce()
{
	print_title "Announce tests"

	# Capture events on the network namespace running the server
	:>"$server_evts"

	# ADD_ADDR using an invalid token should result in no action
	local invalid_token=$(( client4_token - 1))
	ip netns exec "$ns2" ./pm_nl_ctl ann 10.0.2.2 token $invalid_token id\
	   $client_addr_id dev ns2eth1 > /dev/null 2>&1

	local type
	type=$(sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q' "$server_evts")
	print_test "ADD_ADDR 10.0.2.2 (ns2) => ns1, invalid token"
	if [ "$type" = "" ]
	then
		test_pass
	else
		test_fail "type defined: ${type}"
	fi

	# ADD_ADDR from the client to server machine reusing the subflow port
	:>"$server_evts"
	ip netns exec "$ns2"\
	   ./pm_nl_ctl ann 10.0.2.2 token "$client4_token" id $client_addr_id dev\
	   ns2eth1
	print_test "ADD_ADDR id:${client_addr_id} 10.0.2.2 (ns2) => ns1, reuse port"
	sleep 0.5
	verify_announce_event $server_evts $ANNOUNCED $server4_token "10.0.2.2" $client_addr_id \
			      "$client4_port"

	# ADD_ADDR6 from the client to server machine reusing the subflow port
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl ann\
	   dead:beef:2::2 token "$client6_token" id $client_addr_id dev ns2eth1
	print_test "ADD_ADDR6 id:${client_addr_id} dead:beef:2::2 (ns2) => ns1, reuse port"
	sleep 0.5
	verify_announce_event "$server_evts" "$ANNOUNCED" "$server6_token" "dead:beef:2::2"\
			      "$client_addr_id" "$client6_port" "v6"

	# ADD_ADDR from the client to server machine using a new port
	:>"$server_evts"
	client_addr_id=$((client_addr_id+1))
	ip netns exec "$ns2" ./pm_nl_ctl ann 10.0.2.2 token "$client4_token" id\
	   $client_addr_id dev ns2eth1 port $new4_port
	print_test "ADD_ADDR id:${client_addr_id} 10.0.2.2 (ns2) => ns1, new port"
	sleep 0.5
	verify_announce_event "$server_evts" "$ANNOUNCED" "$server4_token" "10.0.2.2"\
			      "$client_addr_id" "$new4_port"

	# Capture events on the network namespace running the client
	:>"$client_evts"

	# ADD_ADDR from the server to client machine reusing the subflow port
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server4_token" id\
	   $server_addr_id dev ns1eth2
	print_test "ADD_ADDR id:${server_addr_id} 10.0.2.1 (ns1) => ns2, reuse port"
	sleep 0.5
	verify_announce_event "$client_evts" "$ANNOUNCED" "$client4_token" "10.0.2.1"\
			      "$server_addr_id" "$app4_port"

	# ADD_ADDR6 from the server to client machine reusing the subflow port
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl ann dead:beef:2::1 token "$server6_token" id\
	   $server_addr_id dev ns1eth2
	print_test "ADD_ADDR6 id:${server_addr_id} dead:beef:2::1 (ns1) => ns2, reuse port"
	sleep 0.5
	verify_announce_event "$client_evts" "$ANNOUNCED" "$client6_token" "dead:beef:2::1"\
			      "$server_addr_id" "$app6_port" "v6"

	# ADD_ADDR from the server to client machine using a new port
	:>"$client_evts"
	server_addr_id=$((server_addr_id+1))
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server4_token" id\
	   $server_addr_id dev ns1eth2 port $new4_port
	print_test "ADD_ADDR id:${server_addr_id} 10.0.2.1 (ns1) => ns2, new port"
	sleep 0.5
	verify_announce_event "$client_evts" "$ANNOUNCED" "$client4_token" "10.0.2.1"\
			      "$server_addr_id" "$new4_port"
}

verify_remove_event()
{
	local evt=$1
	local e_type=$2
	local e_token=$3
	local e_id=$4
	local type
	local token
	local id

	type=$(sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	token=$(sed --unbuffered -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	id=$(sed --unbuffered -n 's/.*\(rem_id:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")

	check_expected "type" "token" "id"
}

test_remove()
{
	print_title "Remove tests"

	# Capture events on the network namespace running the server
	:>"$server_evts"

	# RM_ADDR using an invalid token should result in no action
	local invalid_token=$(( client4_token - 1 ))
	ip netns exec "$ns2" ./pm_nl_ctl rem token $invalid_token id\
	   $client_addr_id > /dev/null 2>&1
	print_test "RM_ADDR id:${client_addr_id} ns2 => ns1, invalid token"
	local type
	type=$(sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q' "$server_evts")
	if [ "$type" = "" ]
	then
		test_pass
	else
		test_fail
	fi

	# RM_ADDR using an invalid addr id should result in no action
	local invalid_id=$(( client_addr_id + 1 ))
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client4_token" id\
	   $invalid_id > /dev/null 2>&1
	print_test "RM_ADDR id:${invalid_id} ns2 => ns1, invalid id"
	type=$(sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q' "$server_evts")
	if [ "$type" = "" ]
	then
		test_pass
	else
		test_fail
	fi

	# RM_ADDR from the client to server machine
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client4_token" id\
	   $client_addr_id
	print_test "RM_ADDR id:${client_addr_id} ns2 => ns1"
	sleep 0.5
	verify_remove_event "$server_evts" "$REMOVED" "$server4_token" "$client_addr_id"

	# RM_ADDR from the client to server machine
	:>"$server_evts"
	client_addr_id=$(( client_addr_id - 1 ))
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client4_token" id\
	   $client_addr_id
	print_test "RM_ADDR id:${client_addr_id} ns2 => ns1"
	sleep 0.5
	verify_remove_event "$server_evts" "$REMOVED" "$server4_token" "$client_addr_id"

	# RM_ADDR6 from the client to server machine
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client6_token" id\
	   $client_addr_id
	print_test "RM_ADDR6 id:${client_addr_id} ns2 => ns1"
	sleep 0.5
	verify_remove_event "$server_evts" "$REMOVED" "$server6_token" "$client_addr_id"

	# Capture events on the network namespace running the client
	:>"$client_evts"

	# RM_ADDR from the server to client machine
	ip netns exec "$ns1" ./pm_nl_ctl rem token "$server4_token" id\
	   $server_addr_id
	print_test "RM_ADDR id:${server_addr_id} ns1 => ns2"
	sleep 0.5
	verify_remove_event "$client_evts" "$REMOVED" "$client4_token" "$server_addr_id"

	# RM_ADDR from the server to client machine
	:>"$client_evts"
	server_addr_id=$(( server_addr_id - 1 ))
	ip netns exec "$ns1" ./pm_nl_ctl rem token "$server4_token" id\
	   $server_addr_id
	print_test "RM_ADDR id:${server_addr_id} ns1 => ns2"
	sleep 0.5
	verify_remove_event "$client_evts" "$REMOVED" "$client4_token" "$server_addr_id"

	# RM_ADDR6 from the server to client machine
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl rem token "$server6_token" id\
	   $server_addr_id
	print_test "RM_ADDR6 id:${server_addr_id} ns1 => ns2"
	sleep 0.5
	verify_remove_event "$client_evts" "$REMOVED" "$client6_token" "$server_addr_id"
}

verify_subflow_events()
{
	local evt=$1
	local e_type=$2
	local e_token=$3
	local e_family=$4
	local e_saddr=$5
	local e_daddr=$6
	local e_dport=$7
	local e_locid=$8
	local e_remid=$9
	shift 2
	local e_from=$8
	local e_to=$9
	local type
	local token
	local family
	local saddr
	local daddr
	local dport
	local locid
	local remid
	local info

	info="${e_saddr} (${e_from}) => ${e_daddr} (${e_to})"

	if [ "$e_type" = "$SUB_ESTABLISHED" ]
	then
		if [ "$e_family" = "$AF_INET6" ]
		then
			print_test "CREATE_SUBFLOW6 ${info}"
		else
			print_test "CREATE_SUBFLOW ${info}"
		fi
	else
		if [ "$e_family" = "$AF_INET6" ]
		then
			print_test "DESTROY_SUBFLOW6 ${info}"
		else
			print_test "DESTROY_SUBFLOW ${info}"
		fi
	fi

	type=$(sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	token=$(sed --unbuffered -n 's/.*\(token:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	family=$(sed --unbuffered -n 's/.*\(family:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	dport=$(sed --unbuffered -n 's/.*\(dport:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	locid=$(sed --unbuffered -n 's/.*\(loc_id:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	remid=$(sed --unbuffered -n 's/.*\(rem_id:\)\([[:digit:]]*\).*$/\2/p;q' "$evt")
	if [ "$family" = "$AF_INET6" ]
	then
		saddr=$(sed --unbuffered -n 's/.*\(saddr6:\)\([0-9a-f:.]*\).*$/\2/p;q' "$evt")
		daddr=$(sed --unbuffered -n 's/.*\(daddr6:\)\([0-9a-f:.]*\).*$/\2/p;q' "$evt")
	else
		saddr=$(sed --unbuffered -n 's/.*\(saddr4:\)\([0-9.]*\).*$/\2/p;q' "$evt")
		daddr=$(sed --unbuffered -n 's/.*\(daddr4:\)\([0-9.]*\).*$/\2/p;q' "$evt")
	fi

	check_expected "type" "token" "daddr" "dport" "family" "saddr" "locid" "remid"
}

test_subflows()
{
	print_title "Subflows v4 or v6 only tests"

	# Capture events on the network namespace running the server
	:>"$server_evts"

	# Attempt to add a listener at 10.0.2.2:<subflow-port>
	ip netns exec "$ns2" ./pm_nl_ctl listen 10.0.2.2\
	   "$client4_port" &
	local listener_pid=$!

	# ADD_ADDR from client to server machine reusing the subflow port
	ip netns exec "$ns2" ./pm_nl_ctl ann 10.0.2.2 token "$client4_token" id\
	   $client_addr_id
	sleep 0.5

	# CREATE_SUBFLOW from server to client machine
	:>"$server_evts"
	ip netns exec "$ns1" ./pm_nl_ctl csf lip 10.0.2.1 lid 23 rip 10.0.2.2\
	   rport "$client4_port" token "$server4_token"
	sleep 0.5
	verify_subflow_events $server_evts $SUB_ESTABLISHED $server4_token $AF_INET "10.0.2.1" \
			      "10.0.2.2" "$client4_port" "23" "$client_addr_id" "ns1" "ns2"

	# Delete the listener from the client ns, if one was created
	kill_wait $listener_pid

	local sport
	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$server_evts")

	# DESTROY_SUBFLOW from server to client machine
	:>"$server_evts"
	ip netns exec "$ns1" ./pm_nl_ctl dsf lip 10.0.2.1 lport "$sport" rip 10.0.2.2 rport\
	   "$client4_port" token "$server4_token"
	sleep 0.5
	verify_subflow_events "$server_evts" "$SUB_CLOSED" "$server4_token" "$AF_INET" "10.0.2.1"\
			      "10.0.2.2" "$client4_port" "23" "$client_addr_id" "ns1" "ns2"

	# RM_ADDR from client to server machine
	ip netns exec "$ns2" ./pm_nl_ctl rem id $client_addr_id token\
	   "$client4_token"
	sleep 0.5

	# Attempt to add a listener at dead:beef:2::2:<subflow-port>
	ip netns exec "$ns2" ./pm_nl_ctl listen dead:beef:2::2\
	   "$client6_port" &
	listener_pid=$!

	# ADD_ADDR6 from client to server machine reusing the subflow port
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl ann dead:beef:2::2 token "$client6_token" id\
	   $client_addr_id
	sleep 0.5

	# CREATE_SUBFLOW6 from server to client machine
	:>"$server_evts"
	ip netns exec "$ns1" ./pm_nl_ctl csf lip dead:beef:2::1 lid 23 rip\
	   dead:beef:2::2 rport "$client6_port" token "$server6_token"
	sleep 0.5
	verify_subflow_events "$server_evts" "$SUB_ESTABLISHED" "$server6_token" "$AF_INET6"\
			      "dead:beef:2::1" "dead:beef:2::2" "$client6_port" "23"\
			      "$client_addr_id" "ns1" "ns2"

	# Delete the listener from the client ns, if one was created
	kill_wait $listener_pid

	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$server_evts")

	# DESTROY_SUBFLOW6 from server to client machine
	:>"$server_evts"
	ip netns exec "$ns1" ./pm_nl_ctl dsf lip dead:beef:2::1 lport "$sport" rip\
	   dead:beef:2::2 rport "$client6_port" token "$server6_token"
	sleep 0.5
	verify_subflow_events "$server_evts" "$SUB_CLOSED" "$server6_token" "$AF_INET6"\
			      "dead:beef:2::1" "dead:beef:2::2" "$client6_port" "23"\
			      "$client_addr_id" "ns1" "ns2"

	# RM_ADDR from client to server machine
	ip netns exec "$ns2" ./pm_nl_ctl rem id $client_addr_id token\
	   "$client6_token"
	sleep 0.5

	# Attempt to add a listener at 10.0.2.2:<new-port>
	ip netns exec "$ns2" ./pm_nl_ctl listen 10.0.2.2\
	   $new4_port &
	listener_pid=$!

	# ADD_ADDR from client to server machine using a new port
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl ann 10.0.2.2 token "$client4_token" id\
	   $client_addr_id port $new4_port
	sleep 0.5

	# CREATE_SUBFLOW from server to client machine
	:>"$server_evts"
	ip netns exec "$ns1" ./pm_nl_ctl csf lip 10.0.2.1 lid 23 rip 10.0.2.2 rport\
	   $new4_port token "$server4_token"
	sleep 0.5
	verify_subflow_events "$server_evts" "$SUB_ESTABLISHED" "$server4_token" "$AF_INET"\
			      "10.0.2.1" "10.0.2.2" "$new4_port" "23"\
			      "$client_addr_id" "ns1" "ns2"

	# Delete the listener from the client ns, if one was created
	kill_wait $listener_pid

	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$server_evts")

	# DESTROY_SUBFLOW from server to client machine
	:>"$server_evts"
	ip netns exec "$ns1" ./pm_nl_ctl dsf lip 10.0.2.1 lport "$sport" rip 10.0.2.2 rport\
	   $new4_port token "$server4_token"
	sleep 0.5
	verify_subflow_events "$server_evts" "$SUB_CLOSED" "$server4_token" "$AF_INET" "10.0.2.1"\
			      "10.0.2.2" "$new4_port" "23" "$client_addr_id" "ns1" "ns2"

	# RM_ADDR from client to server machine
	ip netns exec "$ns2" ./pm_nl_ctl rem id $client_addr_id token\
	   "$client4_token"

	# Capture events on the network namespace running the client
	:>"$client_evts"

	# Attempt to add a listener at 10.0.2.1:<subflow-port>
	ip netns exec "$ns1" ./pm_nl_ctl listen 10.0.2.1\
	   $app4_port &
	listener_pid=$!

	# ADD_ADDR from server to client machine reusing the subflow port
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server4_token" id\
	   $server_addr_id
	sleep 0.5

	# CREATE_SUBFLOW from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl csf lip 10.0.2.2 lid 23 rip 10.0.2.1 rport\
	   $app4_port token "$client4_token"
	sleep 0.5
	verify_subflow_events $client_evts $SUB_ESTABLISHED $client4_token $AF_INET "10.0.2.2"\
			      "10.0.2.1" "$app4_port" "23" "$server_addr_id" "ns2" "ns1"

	# Delete the listener from the server ns, if one was created
	kill_wait $listener_pid

	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$client_evts")

	# DESTROY_SUBFLOW from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl dsf lip 10.0.2.2 lport "$sport" rip 10.0.2.1 rport\
	   $app4_port token "$client4_token"
	sleep 0.5
	verify_subflow_events "$client_evts" "$SUB_CLOSED" "$client4_token" "$AF_INET" "10.0.2.2"\
			      "10.0.2.1" "$app4_port" "23" "$server_addr_id" "ns2" "ns1"

	# RM_ADDR from server to client machine
	ip netns exec "$ns1" ./pm_nl_ctl rem id $server_addr_id token\
	   "$server4_token"
	sleep 0.5

	# Attempt to add a listener at dead:beef:2::1:<subflow-port>
	ip netns exec "$ns1" ./pm_nl_ctl listen dead:beef:2::1\
	   $app6_port &
	listener_pid=$!

	# ADD_ADDR6 from server to client machine reusing the subflow port
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl ann dead:beef:2::1 token "$server6_token" id\
	   $server_addr_id
	sleep 0.5

	# CREATE_SUBFLOW6 from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl csf lip dead:beef:2::2 lid 23 rip\
	   dead:beef:2::1 rport $app6_port token "$client6_token"
	sleep 0.5
	verify_subflow_events "$client_evts" "$SUB_ESTABLISHED" "$client6_token"\
			      "$AF_INET6" "dead:beef:2::2"\
			      "dead:beef:2::1" "$app6_port" "23"\
			      "$server_addr_id" "ns2" "ns1"

	# Delete the listener from the server ns, if one was created
	kill_wait $listener_pid

	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$client_evts")

	# DESTROY_SUBFLOW6 from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl dsf lip dead:beef:2::2 lport "$sport" rip\
	   dead:beef:2::1 rport $app6_port token "$client6_token"
	sleep 0.5
	verify_subflow_events $client_evts $SUB_CLOSED $client6_token $AF_INET6 "dead:beef:2::2"\
			      "dead:beef:2::1" "$app6_port" "23" "$server_addr_id" "ns2" "ns1"

	# RM_ADDR6 from server to client machine
	ip netns exec "$ns1" ./pm_nl_ctl rem id $server_addr_id token\
	   "$server6_token"
	sleep 0.5

	# Attempt to add a listener at 10.0.2.1:<new-port>
	ip netns exec "$ns1" ./pm_nl_ctl listen 10.0.2.1\
	   $new4_port &
	listener_pid=$!

	# ADD_ADDR from server to client machine using a new port
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server4_token" id\
	   $server_addr_id port $new4_port
	sleep 0.5

	# CREATE_SUBFLOW from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl csf lip 10.0.2.2 lid 23 rip 10.0.2.1 rport\
	   $new4_port token "$client4_token"
	sleep 0.5
	verify_subflow_events "$client_evts" "$SUB_ESTABLISHED" "$client4_token" "$AF_INET"\
			      "10.0.2.2" "10.0.2.1" "$new4_port" "23" "$server_addr_id" "ns2" "ns1"

	# Delete the listener from the server ns, if one was created
	kill_wait $listener_pid

	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$client_evts")

	# DESTROY_SUBFLOW from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl dsf lip 10.0.2.2 lport "$sport" rip 10.0.2.1 rport\
	   $new4_port token "$client4_token"
	sleep 0.5
	verify_subflow_events "$client_evts" "$SUB_CLOSED" "$client4_token" "$AF_INET" "10.0.2.2"\
			      "10.0.2.1" "$new4_port" "23" "$server_addr_id" "ns2" "ns1"

	# RM_ADDR from server to client machine
	ip netns exec "$ns1" ./pm_nl_ctl rem id $server_addr_id token\
	   "$server4_token"
}

test_subflows_v4_v6_mix()
{
	print_title "Subflows v4 and v6 mix tests"

	# Attempt to add a listener at 10.0.2.1:<subflow-port>
	ip netns exec "$ns1" ./pm_nl_ctl listen 10.0.2.1\
	   $app6_port &
	local listener_pid=$!

	# ADD_ADDR4 from server to client machine reusing the subflow port on
	# the established v6 connection
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server6_token" id\
	   $server_addr_id dev ns1eth2
	print_test "ADD_ADDR4 id:${server_addr_id} 10.0.2.1 (ns1) => ns2, reuse port"
	sleep 0.5
	verify_announce_event "$client_evts" "$ANNOUNCED" "$client6_token" "10.0.2.1"\
			      "$server_addr_id" "$app6_port"

	# CREATE_SUBFLOW from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl csf lip 10.0.2.2 lid 23 rip 10.0.2.1 rport\
	   $app6_port token "$client6_token"
	sleep 0.5
	verify_subflow_events "$client_evts" "$SUB_ESTABLISHED" "$client6_token"\
			      "$AF_INET" "10.0.2.2" "10.0.2.1" "$app6_port" "23"\
			      "$server_addr_id" "ns2" "ns1"

	# Delete the listener from the server ns, if one was created
	kill_wait $listener_pid

	sport=$(sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q' "$client_evts")

	# DESTROY_SUBFLOW from client to server machine
	:>"$client_evts"
	ip netns exec "$ns2" ./pm_nl_ctl dsf lip 10.0.2.2 lport "$sport" rip 10.0.2.1 rport\
	   $app6_port token "$client6_token"
	sleep 0.5
	verify_subflow_events "$client_evts" "$SUB_CLOSED" "$client6_token" \
			      "$AF_INET" "10.0.2.2" "10.0.2.1" "$app6_port" "23"\
			      "$server_addr_id" "ns2" "ns1"

	# RM_ADDR from server to client machine
	ip netns exec "$ns1" ./pm_nl_ctl rem id $server_addr_id token\
	   "$server6_token"
	sleep 0.5
}

test_prio()
{
	print_title "Prio tests"

	local count

	# Send MP_PRIO signal from client to server machine
	ip netns exec "$ns2" ./pm_nl_ctl set 10.0.1.2 port "$client4_port" flags backup token "$client4_token" rip 10.0.1.1 rport "$app4_port"
	sleep 0.5

	# Check TX
	print_test "MP_PRIO TX"
	count=$(ip netns exec "$ns2" nstat -as | grep MPTcpExtMPPrioTx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ $count != 1 ]; then
		test_fail "Count != 1: ${count}"
	else
		test_pass
	fi

	# Check RX
	print_test "MP_PRIO RX"
	count=$(ip netns exec "$ns1" nstat -as | grep MPTcpExtMPPrioRx | awk '{print $2}')
	[ -z "$count" ] && count=0
	if [ $count != 1 ]; then
		test_fail "Count != 1: ${count}"
	else
		test_pass
	fi
}

verify_listener_events()
{
	local evt=$1
	local e_type=$2
	local e_family=$3
	local e_saddr=$4
	local e_sport=$5
	local type
	local family
	local saddr
	local sport

	if [ $e_type = $LISTENER_CREATED ]; then
		print_test "CREATE_LISTENER $e_saddr:$e_sport"
	elif [ $e_type = $LISTENER_CLOSED ]; then
		print_test "CLOSE_LISTENER $e_saddr:$e_sport"
	fi

	type=$(grep "type:$e_type," $evt |
	       sed --unbuffered -n 's/.*\(type:\)\([[:digit:]]*\).*$/\2/p;q')
	family=$(grep "type:$e_type," $evt |
		 sed --unbuffered -n 's/.*\(family:\)\([[:digit:]]*\).*$/\2/p;q')
	sport=$(grep "type:$e_type," $evt |
		sed --unbuffered -n 's/.*\(sport:\)\([[:digit:]]*\).*$/\2/p;q')
	if [ $family ] && [ $family = $AF_INET6 ]; then
		saddr=$(grep "type:$e_type," $evt |
			sed --unbuffered -n 's/.*\(saddr6:\)\([0-9a-f:.]*\).*$/\2/p;q')
	else
		saddr=$(grep "type:$e_type," $evt |
			sed --unbuffered -n 's/.*\(saddr4:\)\([0-9.]*\).*$/\2/p;q')
	fi

	check_expected "type" "family" "saddr" "sport"
}

test_listener()
{
	print_title "Listener tests"

	if ! mptcp_lib_kallsyms_has "mptcp_event_pm_listener$"; then
		print_test "LISTENER events"
		test_skip
		return
	fi

	# Capture events on the network namespace running the client
	:>$client_evts

	# Attempt to add a listener at 10.0.2.2:<subflow-port>
	ip netns exec $ns2 ./pm_nl_ctl listen 10.0.2.2\
		$client4_port &
	local listener_pid=$!

	sleep 0.5
	verify_listener_events $client_evts $LISTENER_CREATED $AF_INET 10.0.2.2 $client4_port

	# ADD_ADDR from client to server machine reusing the subflow port
	ip netns exec $ns2 ./pm_nl_ctl ann 10.0.2.2 token $client4_token id\
		$client_addr_id
	sleep 0.5

	# CREATE_SUBFLOW from server to client machine
	ip netns exec $ns1 ./pm_nl_ctl csf lip 10.0.2.1 lid 23 rip 10.0.2.2\
		rport $client4_port token $server4_token
	sleep 0.5

	# Delete the listener from the client ns, if one was created
	kill_wait $listener_pid

	sleep 0.5
	verify_listener_events $client_evts $LISTENER_CLOSED $AF_INET 10.0.2.2 $client4_port
}

print_title "Make connections"
make_connection
make_connection "v6"

test_announce
test_remove
test_subflows
test_subflows_v4_v6_mix
test_prio
test_listener

mptcp_lib_result_print_all_tap
exit ${ret}
