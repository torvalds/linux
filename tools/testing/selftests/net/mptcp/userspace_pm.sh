#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Double quotes to prevent globbing and word splitting is recommended in new
# code but we accept it.
#shellcheck disable=SC2086

# Some variables are used below but indirectly, see verify_*_event()
#shellcheck disable=SC2034

. "$(dirname "${0}")/mptcp_lib.sh"

mptcp_lib_check_mptcp
mptcp_lib_check_kallsyms

if ! mptcp_lib_has_file '/proc/sys/net/mptcp/pm_type'; then
	echo "userspace pm tests are not supported by the kernel: SKIP"
	exit ${KSFT_SKIP}
fi
mptcp_lib_check_tools ip

ANNOUNCED=${MPTCP_LIB_EVENT_ANNOUNCED}
REMOVED=${MPTCP_LIB_EVENT_REMOVED}
SUB_ESTABLISHED=${MPTCP_LIB_EVENT_SUB_ESTABLISHED}
SUB_CLOSED=${MPTCP_LIB_EVENT_SUB_CLOSED}
LISTENER_CREATED=${MPTCP_LIB_EVENT_LISTENER_CREATED}
LISTENER_CLOSED=${MPTCP_LIB_EVENT_LISTENER_CLOSED}

AF_INET=${MPTCP_LIB_AF_INET}
AF_INET6=${MPTCP_LIB_AF_INET6}

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

ns1=""
ns2=""
ret=0
test_name=""
# a bit more space: because we have more to display
MPTCP_LIB_TEST_FORMAT="%02u %-68s"

print_title()
{
	mptcp_lib_pr_info "${1}"
}

# $1: test name
print_test()
{
	test_name="${1}"

	mptcp_lib_print_title "${test_name}"
}

test_pass()
{
	mptcp_lib_pr_ok
	mptcp_lib_result_pass "${test_name}"
}

test_skip()
{
	mptcp_lib_pr_skip
	mptcp_lib_result_skip "${test_name}"
}

# $1: msg
test_fail()
{
	if [ ${#} -gt 0 ]
	then
		mptcp_lib_pr_fail "${@}"
	fi
	ret=${KSFT_FAIL}
	mptcp_lib_result_fail "${test_name}"
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
		mptcp_lib_kill_wait $pid
	done

	mptcp_lib_ns_exit "${ns1}" "${ns2}"

	rm -rf $file $client_evts $server_evts

	mptcp_lib_pr_info "Done"
}

trap cleanup EXIT

# Create and configure network namespaces for testing
print_title "Init"
mptcp_lib_ns_init ns1 ns2

# check path_manager and pm_type sysctl mapping
if [ -f /proc/sys/net/mptcp/path_manager ]; then
	ip netns exec "$ns1" sysctl -q net.mptcp.path_manager=userspace
	pm_type="$(ip netns exec "$ns1" sysctl -n net.mptcp.pm_type)"
	if [ "${pm_type}" != "1" ]; then
		test_fail "unexpected pm_type: ${pm_type}"
		mptcp_lib_result_print_all_tap
		exit ${KSFT_FAIL}
	fi

	ip netns exec "$ns1" sysctl -q net.mptcp.path_manager=error 2>/dev/null
	pm_type="$(ip netns exec "$ns1" sysctl -n net.mptcp.pm_type)"
	if [ "${pm_type}" != "1" ]; then
		test_fail "unexpected pm_type after error: ${pm_type}"
		mptcp_lib_result_print_all_tap
		exit ${KSFT_FAIL}
	fi

	ip netns exec "$ns1" sysctl -q net.mptcp.pm_type=0
	pm_name="$(ip netns exec "$ns1" sysctl -n net.mptcp.path_manager)"
	if [ "${pm_name}" != "kernel" ]; then
		test_fail "unexpected path-manager: ${pm_name}"
		mptcp_lib_result_print_all_tap
		exit ${KSFT_FAIL}
	fi
fi

for i in "$ns1" "$ns2" ;do
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

file=$(mktemp)
mptcp_lib_make_file "$file" 2 1

# Capture netlink events over the two network namespaces running
# the MPTCP client and server
client_evts=$(mktemp)
mptcp_lib_events "${ns2}" "${client_evts}" client_evts_pid
server_evts=$(mktemp)
mptcp_lib_events "${ns1}" "${server_evts}" server_evts_pid
sleep 0.5
mptcp_lib_subtests_last_ts_reset

print_test "Created network namespaces ns1, ns2"
test_pass

make_connection()
{
	local is_v6=$1
	local app_port=$app4_port
	local connect_addr="10.0.1.1"
	local client_addr="10.0.1.2"
	local listen_addr="0.0.0.0"
	if [ "$is_v6" = "v6" ]
	then
		connect_addr="dead:beef:1::1"
		client_addr="dead:beef:1::2"
		listen_addr="::"
		app_port=$app6_port
	else
		is_v6="v4"
	fi

	:>"$client_evts"
	:>"$server_evts"

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

	client_token=$(mptcp_lib_evts_get_info token "$client_evts")
	client_port=$(mptcp_lib_evts_get_info sport "$client_evts")
	client_serverside=$(mptcp_lib_evts_get_info server_side "$client_evts")
	server_token=$(mptcp_lib_evts_get_info token "$server_evts")
	server_serverside=$(mptcp_lib_evts_get_info server_side "$server_evts")

	print_test "Established IP${is_v6} MPTCP Connection ns2 => ns1"
	if [ "$client_token" != "" ] && [ "$server_token" != "" ] && [ "$client_serverside" = 0 ] &&
		   [ "$server_serverside" = 1 ]
	then
		test_pass
		print_title "Connection info: ${client_addr}:${client_port} -> ${connect_addr}:${app_port}"
	else
		test_fail "Expected tokens (c:${client_token} - s:${server_token}) and server (c:${client_serverside} - s:${server_serverside})"
		mptcp_lib_result_print_all_tap
		exit ${KSFT_FAIL}
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

# $@: all var names to check
check_expected()
{
	if mptcp_lib_check_expected "${@}"
	then
		test_pass
		return 0
	fi

	test_fail
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

	type=$(mptcp_lib_evts_get_info type "$evt" $e_type)
	token=$(mptcp_lib_evts_get_info token "$evt" $e_type)
	if [ "$e_af" = "v6" ]
	then
		addr=$(mptcp_lib_evts_get_info daddr6 "$evt" $e_type)
	else
		addr=$(mptcp_lib_evts_get_info daddr4 "$evt" $e_type)
	fi
	dport=$(mptcp_lib_evts_get_info dport "$evt" $e_type)
	id=$(mptcp_lib_evts_get_info rem_id "$evt" $e_type)

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
	type=$(mptcp_lib_evts_get_info type "$server_evts")
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
	print_test "ADD_ADDR id:client 10.0.2.2 (ns2) => ns1, reuse port"
	sleep 0.5
	verify_announce_event $server_evts $ANNOUNCED $server4_token "10.0.2.2" $client_addr_id \
			      "$client4_port"

	# ADD_ADDR6 from the client to server machine reusing the subflow port
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl ann\
	   dead:beef:2::2 token "$client6_token" id $client_addr_id dev ns2eth1
	print_test "ADD_ADDR6 id:client dead:beef:2::2 (ns2) => ns1, reuse port"
	sleep 0.5
	verify_announce_event "$server_evts" "$ANNOUNCED" "$server6_token" "dead:beef:2::2"\
			      "$client_addr_id" "$client6_port" "v6"

	# ADD_ADDR from the client to server machine using a new port
	:>"$server_evts"
	client_addr_id=$((client_addr_id+1))
	ip netns exec "$ns2" ./pm_nl_ctl ann 10.0.2.2 token "$client4_token" id\
	   $client_addr_id dev ns2eth1 port $new4_port
	print_test "ADD_ADDR id:client+1 10.0.2.2 (ns2) => ns1, new port"
	sleep 0.5
	verify_announce_event "$server_evts" "$ANNOUNCED" "$server4_token" "10.0.2.2"\
			      "$client_addr_id" "$new4_port"

	# Capture events on the network namespace running the client
	:>"$client_evts"

	# ADD_ADDR from the server to client machine reusing the subflow port
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server4_token" id\
	   $server_addr_id dev ns1eth2
	print_test "ADD_ADDR id:server 10.0.2.1 (ns1) => ns2, reuse port"
	sleep 0.5
	verify_announce_event "$client_evts" "$ANNOUNCED" "$client4_token" "10.0.2.1"\
			      "$server_addr_id" "$app4_port"

	# ADD_ADDR6 from the server to client machine reusing the subflow port
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl ann dead:beef:2::1 token "$server6_token" id\
	   $server_addr_id dev ns1eth2
	print_test "ADD_ADDR6 id:server dead:beef:2::1 (ns1) => ns2, reuse port"
	sleep 0.5
	verify_announce_event "$client_evts" "$ANNOUNCED" "$client6_token" "dead:beef:2::1"\
			      "$server_addr_id" "$app6_port" "v6"

	# ADD_ADDR from the server to client machine using a new port
	:>"$client_evts"
	server_addr_id=$((server_addr_id+1))
	ip netns exec "$ns1" ./pm_nl_ctl ann 10.0.2.1 token "$server4_token" id\
	   $server_addr_id dev ns1eth2 port $new4_port
	print_test "ADD_ADDR id:server+1 10.0.2.1 (ns1) => ns2, new port"
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

	type=$(mptcp_lib_evts_get_info type "$evt" $e_type)
	token=$(mptcp_lib_evts_get_info token "$evt" $e_type)
	id=$(mptcp_lib_evts_get_info rem_id "$evt" $e_type)

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
	print_test "RM_ADDR id:client ns2 => ns1, invalid token"
	local type
	type=$(mptcp_lib_evts_get_info type "$server_evts")
	if [ "$type" = "" ]
	then
		test_pass
	else
		test_fail "unexpected type: ${type}"
	fi

	# RM_ADDR using an invalid addr id should result in no action
	local invalid_id=$(( client_addr_id + 1 ))
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client4_token" id\
	   $invalid_id > /dev/null 2>&1
	print_test "RM_ADDR id:client+1 ns2 => ns1, invalid id"
	type=$(mptcp_lib_evts_get_info type "$server_evts")
	if [ "$type" = "" ]
	then
		test_pass
	else
		test_fail "unexpected type: ${type}"
	fi

	# RM_ADDR from the client to server machine
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client4_token" id\
	   $client_addr_id
	print_test "RM_ADDR id:client ns2 => ns1"
	sleep 0.5
	verify_remove_event "$server_evts" "$REMOVED" "$server4_token" "$client_addr_id"

	# RM_ADDR from the client to server machine
	:>"$server_evts"
	client_addr_id=$(( client_addr_id - 1 ))
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client4_token" id\
	   $client_addr_id
	print_test "RM_ADDR id:client-1 ns2 => ns1"
	sleep 0.5
	verify_remove_event "$server_evts" "$REMOVED" "$server4_token" "$client_addr_id"

	# RM_ADDR6 from the client to server machine
	:>"$server_evts"
	ip netns exec "$ns2" ./pm_nl_ctl rem token "$client6_token" id\
	   $client_addr_id
	print_test "RM_ADDR6 id:client-1 ns2 => ns1"
	sleep 0.5
	verify_remove_event "$server_evts" "$REMOVED" "$server6_token" "$client_addr_id"

	# Capture events on the network namespace running the client
	:>"$client_evts"

	# RM_ADDR from the server to client machine
	ip netns exec "$ns1" ./pm_nl_ctl rem token "$server4_token" id\
	   $server_addr_id
	print_test "RM_ADDR id:server ns1 => ns2"
	sleep 0.5
	verify_remove_event "$client_evts" "$REMOVED" "$client4_token" "$server_addr_id"

	# RM_ADDR from the server to client machine
	:>"$client_evts"
	server_addr_id=$(( server_addr_id - 1 ))
	ip netns exec "$ns1" ./pm_nl_ctl rem token "$server4_token" id\
	   $server_addr_id
	print_test "RM_ADDR id:server-1 ns1 => ns2"
	sleep 0.5
	verify_remove_event "$client_evts" "$REMOVED" "$client4_token" "$server_addr_id"

	# RM_ADDR6 from the server to client machine
	:>"$client_evts"
	ip netns exec "$ns1" ./pm_nl_ctl rem token "$server6_token" id\
	   $server_addr_id
	print_test "RM_ADDR6 id:server-1 ns1 => ns2"
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
	local e_dport_txt

	# only display the fixed ports
	if [ "${e_dport}" -ge "${app4_port}" ] && [ "${e_dport}" -le "${app6_port}" ]; then
		e_dport_txt=":${e_dport}"
	fi

	info="${e_saddr} (${e_from}) => ${e_daddr}${e_dport_txt} (${e_to})"

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

	type=$(mptcp_lib_evts_get_info type "$evt" $e_type)
	token=$(mptcp_lib_evts_get_info token "$evt" $e_type)
	family=$(mptcp_lib_evts_get_info family "$evt" $e_type)
	dport=$(mptcp_lib_evts_get_info dport "$evt" $e_type)
	locid=$(mptcp_lib_evts_get_info loc_id "$evt" $e_type)
	remid=$(mptcp_lib_evts_get_info rem_id "$evt" $e_type)
	if [ "$family" = "$AF_INET6" ]
	then
		saddr=$(mptcp_lib_evts_get_info saddr6 "$evt" $e_type)
		daddr=$(mptcp_lib_evts_get_info daddr6 "$evt" $e_type)
	else
		saddr=$(mptcp_lib_evts_get_info saddr4 "$evt" $e_type)
		daddr=$(mptcp_lib_evts_get_info daddr4 "$evt" $e_type)
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
	mptcp_lib_kill_wait $listener_pid

	local sport
	sport=$(mptcp_lib_evts_get_info sport "$server_evts" $SUB_ESTABLISHED)

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
	mptcp_lib_kill_wait $listener_pid

	sport=$(mptcp_lib_evts_get_info sport "$server_evts" $SUB_ESTABLISHED)

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
	mptcp_lib_kill_wait $listener_pid

	sport=$(mptcp_lib_evts_get_info sport "$server_evts" $SUB_ESTABLISHED)

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
	mptcp_lib_kill_wait $listener_pid

	sport=$(mptcp_lib_evts_get_info sport "$client_evts" $SUB_ESTABLISHED)

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
	mptcp_lib_kill_wait $listener_pid

	sport=$(mptcp_lib_evts_get_info sport "$client_evts" $SUB_ESTABLISHED)

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
	mptcp_lib_kill_wait $listener_pid

	sport=$(mptcp_lib_evts_get_info sport "$client_evts" $SUB_ESTABLISHED)

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
	print_test "ADD_ADDR4 id:server 10.0.2.1 (ns1) => ns2, reuse port"
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
	mptcp_lib_kill_wait $listener_pid

	sport=$(mptcp_lib_evts_get_info sport "$client_evts" $SUB_ESTABLISHED)

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
	count=$(mptcp_lib_get_counter "$ns2" "MPTcpExtMPPrioTx")
	if [ -z "$count" ]; then
		test_skip
	elif [ $count != 1 ]; then
		test_fail "Count != 1: ${count}"
	else
		test_pass
	fi

	# Check RX
	print_test "MP_PRIO RX"
	count=$(mptcp_lib_get_counter "$ns1" "MPTcpExtMPPrioRx")
	if [ -z "$count" ]; then
		test_skip
	elif [ $count != 1 ]; then
		test_fail "Count != 1: ${count}"
	else
		test_pass
	fi
}

verify_listener_events()
{
	if mptcp_lib_verify_listener_events "${@}"; then
		test_pass
	else
		test_fail
	fi
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
	print_test "CREATE_LISTENER 10.0.2.2 (client port)"
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
	mptcp_lib_kill_wait $listener_pid

	sleep 0.5
	print_test "CLOSE_LISTENER 10.0.2.2 (client port)"
	verify_listener_events $client_evts $LISTENER_CLOSED $AF_INET 10.0.2.2 $client4_port
}

print_title "Make connections"
make_connection
make_connection "v6"
print_title "Will be using address IDs ${client_addr_id} (client) and ${server_addr_id} (server)"

test_announce
test_remove
test_subflows
test_subflows_v4_v6_mix
test_prio
test_listener

mptcp_lib_result_print_all_tap
exit ${ret}
