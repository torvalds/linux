#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# OVS kernel module self tests

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

PAUSE_ON_FAIL=no
VERBOSE=0
TRACING=0

tests="
	arp_ping				eth-arp: Basic arp ping between two NS
	ct_connect_v4				ip4-ct-xon: Basic ipv4 tcp connection using ct
	connect_v4				ip4-xon: Basic ipv4 ping between two NS
	nat_connect_v4				ip4-nat-xon: Basic ipv4 tcp connection via NAT
	netlink_checks				ovsnl: validate netlink attrs and settings
	upcall_interfaces			ovs: test the upcall interfaces
	drop_reason				drop: test drop reasons are emitted"

info() {
    [ $VERBOSE = 0 ] || echo $*
}

ovs_base=`pwd`
sbxs=
sbx_add () {
	info "adding sandbox '$1'"

	sbxs="$sbxs $1"

	NO_BIN=0

	# Create sandbox.
	local d="$ovs_base"/$1
	if [ -e $d ]; then
		info "removing $d"
		rm -rf "$d"
	fi
	mkdir "$d" || return 1
	ovs_setenv $1
}

ovs_exit_sig() {
	[ -e ${ovs_dir}/cleanup ] && . "$ovs_dir/cleanup"
}

on_exit() {
	echo "$1" > ${ovs_dir}/cleanup.tmp
	cat ${ovs_dir}/cleanup >> ${ovs_dir}/cleanup.tmp
	mv ${ovs_dir}/cleanup.tmp ${ovs_dir}/cleanup
}

ovs_setenv() {
	sandbox=$1

	ovs_dir=$ovs_base${1:+/$1}; export ovs_dir

	test -e ${ovs_dir}/cleanup || : > ${ovs_dir}/cleanup
}

ovs_sbx() {
	if test "X$2" != X; then
		(ovs_setenv $1; shift; "$@" >> ${ovs_dir}/debug.log)
	else
		ovs_setenv $1
	fi
}

ovs_add_dp () {
	info "Adding DP/Bridge IF: sbx:$1 dp:$2 {$3, $4, $5}"
	sbxname="$1"
	shift
	ovs_sbx "$sbxname" python3 $ovs_base/ovs-dpctl.py add-dp $*
	on_exit "ovs_sbx $sbxname python3 $ovs_base/ovs-dpctl.py del-dp $1;"
}

ovs_add_if () {
	info "Adding IF to DP: br:$2 if:$3"
	if [ "$4" != "-u" ]; then
		ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py add-if "$2" "$3" \
		    || return 1
	else
		python3 $ovs_base/ovs-dpctl.py add-if \
		    -u "$2" "$3" >$ovs_dir/$3.out 2>$ovs_dir/$3.err &
		pid=$!
		on_exit "ovs_sbx $1 kill -TERM $pid 2>/dev/null"
	fi
}

ovs_del_if () {
	info "Deleting IF from DP: br:$2 if:$3"
	ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py del-if "$2" "$3" || return 1
}

ovs_netns_spawn_daemon() {
	sbx=$1
	shift
	netns=$1
	shift
	info "spawning cmd: $*"
	ip netns exec $netns $*  >> $ovs_dir/stdout  2>> $ovs_dir/stderr &
	pid=$!
	ovs_sbx "$sbx" on_exit "kill -TERM $pid 2>/dev/null"
}

ovs_add_netns_and_veths () {
	info "Adding netns attached: sbx:$1 dp:$2 {$3, $4, $5}"
	ovs_sbx "$1" ip netns add "$3" || return 1
	on_exit "ovs_sbx $1 ip netns del $3"
	ovs_sbx "$1" ip link add "$4" type veth peer name "$5" || return 1
	on_exit "ovs_sbx $1 ip link del $4 >/dev/null 2>&1"
	ovs_sbx "$1" ip link set "$4" up || return 1
	ovs_sbx "$1" ip link set "$5" netns "$3" || return 1
	ovs_sbx "$1" ip netns exec "$3" ip link set "$5" up || return 1

	if [ "$6" != "" ]; then
		ovs_sbx "$1" ip netns exec "$3" ip addr add "$6" dev "$5" \
		    || return 1
	fi

	if [ "$7" != "-u" ]; then
		ovs_add_if "$1" "$2" "$4" || return 1
	else
		ovs_add_if "$1" "$2" "$4" -u || return 1
	fi

	[ $TRACING -eq 1 ] && ovs_netns_spawn_daemon "$1" "$ns" \
			tcpdump -i any -s 65535

	return 0
}

ovs_add_flow () {
	info "Adding flow to DP: sbx:$1 br:$2 flow:$3 act:$4"
	ovs_sbx "$1" python3 $ovs_base/ovs-dpctl.py add-flow "$2" "$3" "$4"
	if [ $? -ne 0 ]; then
		echo "Flow [ $3 : $4 ] failed" >> ${ovs_dir}/debug.log
		return 1
	fi
	return 0
}

ovs_drop_record_and_run () {
	local sbx=$1
	shift

	perf record -a -q -e skb:kfree_skb -o ${ovs_dir}/perf.data $* \
		>> ${ovs_dir}/stdout 2>> ${ovs_dir}/stderr
	return $?
}

ovs_drop_reason_count()
{
	local reason=$1

	local perf_output=`perf script -i ${ovs_dir}/perf.data -F trace:event,trace`
	local pattern="skb:kfree_skb:.*reason: $reason"

	return `echo "$perf_output" | grep "$pattern" | wc -l`
}

usage() {
	echo
	echo "$0 [OPTIONS] [TEST]..."
	echo "If no TEST argument is given, all tests will be run."
	echo
	echo "Options"
	echo "  -t: capture traffic via tcpdump"
	echo "  -v: verbose"
	echo "  -p: pause on failure"
	echo
	echo "Available tests${tests}"
	exit 1
}

# drop_reason test
# - drop packets and verify the right drop reason is reported
test_drop_reason() {
	which perf >/dev/null 2>&1 || return $ksft_skip

	sbx_add "test_drop_reason" || return $?

	ovs_add_dp "test_drop_reason" dropreason || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_drop_reason" "dropreason" "$ns" \
			"${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	# Setup client namespace
	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up

	# Setup server namespace
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	# Allow ARP
	ovs_add_flow "test_drop_reason" dropreason \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_drop_reason" dropreason \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1

	# Allow client ICMP traffic but drop return path
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10,proto=1),icmp()" '2'
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(2),eth(),eth_type(0x0800),ipv4(src=172.31.110.20,proto=1),icmp()" 'drop'

	ovs_drop_record_and_run "test_drop_reason" ip netns exec client ping -c 2 172.31.110.20
	ovs_drop_reason_count 0x30001 # OVS_DROP_FLOW_ACTION
	if [[ "$?" -ne "2" ]]; then
		info "Did not detect expected drops: $?"
		return 1
	fi

	# Drop UDP 6000 traffic with an explicit action and an error code.
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10,proto=17),udp(dst=6000)" \
                'drop(42)'
	# Drop UDP 7000 traffic with an explicit action with no error code.
	ovs_add_flow "test_drop_reason" dropreason \
		"in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10,proto=17),udp(dst=7000)" \
                'drop(0)'

	ovs_drop_record_and_run \
            "test_drop_reason" ip netns exec client nc -i 1 -zuv 172.31.110.20 6000
	ovs_drop_reason_count 0x30004 # OVS_DROP_EXPLICIT_ACTION_ERROR
	if [[ "$?" -ne "1" ]]; then
		info "Did not detect expected explicit error drops: $?"
		return 1
	fi

	ovs_drop_record_and_run \
            "test_drop_reason" ip netns exec client nc -i 1 -zuv 172.31.110.20 7000
	ovs_drop_reason_count 0x30003 # OVS_DROP_EXPLICIT_ACTION
	if [[ "$?" -ne "1" ]]; then
		info "Did not detect expected explicit drops: $?"
		return 1
	fi

	return 0
}

# arp_ping test
# - client has 1500 byte MTU
# - server has 1500 byte MTU
# - send ARP ping between two ns
test_arp_ping () {

	which arping >/dev/null 2>&1 || return $ksft_skip

	sbx_add "test_arp_ping" || return $?

	ovs_add_dp "test_arp_ping" arpping || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_arp_ping" "arpping" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	# Setup client namespace
	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	HW_CLIENT=`ip netns exec client ip link show dev c1 | grep -E 'link/ether [0-9a-f:]+' | awk '{print $2;}'`
	info "Client hwaddr: $HW_CLIENT"

	# Setup server namespace
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up
	HW_SERVER=`ip netns exec server ip link show dev s1 | grep -E 'link/ether [0-9a-f:]+' | awk '{print $2;}'`
	info "Server hwaddr: $HW_SERVER"

	ovs_add_flow "test_arp_ping" arpping \
		"in_port(1),eth(),eth_type(0x0806),arp(sip=172.31.110.10,tip=172.31.110.20,sha=$HW_CLIENT,tha=ff:ff:ff:ff:ff:ff)" '2' || return 1
	ovs_add_flow "test_arp_ping" arpping \
		"in_port(2),eth(),eth_type(0x0806),arp()" '1' || return 1

	ovs_sbx "test_arp_ping" ip netns exec client arping -I c1 172.31.110.20 -c 1 || return 1

	return 0
}

# ct_connect_v4 test
#  - client has 1500 byte MTU
#  - server has 1500 byte MTU
#  - use ICMP to ping in each direction
#  - only allow CT state stuff to pass through new in c -> s
test_ct_connect_v4 () {

	which nc >/dev/null 2>/dev/null || return $ksft_skip

	sbx_add "test_ct_connect_v4" || return $?

	ovs_add_dp "test_ct_connect_v4" ct4 || return 1
	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_ct_connect_v4" "ct4" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	# Add forwarding for ARP and ip packets - completely wildcarded
	ovs_add_flow "test_ct_connect_v4" ct4 \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'ct_state(-trk),eth(),eth_type(0x0800),ipv4()' \
		     'ct(commit),recirc(0x1)' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+new),in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10)' \
		     '2' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+est),in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10)' \
		     '2' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+est),in_port(2),eth(),eth_type(0x0800),ipv4(dst=172.31.110.10)' \
		     '1' || return 1
	ovs_add_flow "test_ct_connect_v4" ct4 \
		     'recirc_id(0x1),ct_state(+trk+inv),eth(),eth_type(0x0800),ipv4()' 'drop' || \
		     return 1

	# do a ping
	ovs_sbx "test_ct_connect_v4" ip netns exec client ping 172.31.110.20 -c 3 || return 1

	# create an echo server in 'server'
	echo "server" | \
		ovs_netns_spawn_daemon "test_ct_connect_v4" "server" \
				nc -lvnp 4443
	ovs_sbx "test_ct_connect_v4" ip netns exec client nc -i 1 -zv 172.31.110.20 4443 || return 1

	# Now test in the other direction (should fail)
	echo "client" | \
		ovs_netns_spawn_daemon "test_ct_connect_v4" "client" \
				nc -lvnp 4443
	ovs_sbx "test_ct_connect_v4" ip netns exec client nc -i 1 -zv 172.31.110.10 4443
	if [ $? == 0 ]; then
	   info "ct connect to client was successful"
	   return 1
	fi

	info "done..."
	return 0
}

# connect_v4 test
#  - client has 1500 byte MTU
#  - server has 1500 byte MTU
#  - use ICMP to ping in each direction
test_connect_v4 () {

	sbx_add "test_connect_v4" || return $?

	ovs_add_dp "test_connect_v4" cv4 || return 1

	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_connect_v4" "cv4" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done


	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	# Add forwarding for ARP and ip packets - completely wildcarded
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(1),eth(),eth_type(0x0800),ipv4(src=172.31.110.10)' '2' || return 1
	ovs_add_flow "test_connect_v4" cv4 \
		'in_port(2),eth(),eth_type(0x0800),ipv4(src=172.31.110.20)' '1' || return 1

	# do a ping
	ovs_sbx "test_connect_v4" ip netns exec client ping 172.31.110.20 -c 3 || return 1

	info "done..."
	return 0
}

# nat_connect_v4 test
#  - client has 1500 byte MTU
#  - server has 1500 byte MTU
#  - use ICMP to ping in each direction
#  - only allow CT state stuff to pass through new in c -> s
test_nat_connect_v4 () {
	which nc >/dev/null 2>/dev/null || return $ksft_skip

	sbx_add "test_nat_connect_v4" || return $?

	ovs_add_dp "test_nat_connect_v4" nat4 || return 1
	info "create namespaces"
	for ns in client server; do
		ovs_add_netns_and_veths "test_nat_connect_v4" "nat4" "$ns" \
		    "${ns:0:1}0" "${ns:0:1}1" || return 1
	done

	ip netns exec client ip addr add 172.31.110.10/24 dev c1
	ip netns exec client ip link set c1 up
	ip netns exec server ip addr add 172.31.110.20/24 dev s1
	ip netns exec server ip link set s1 up

	ip netns exec client ip route add default via 172.31.110.20

	ovs_add_flow "test_nat_connect_v4" nat4 \
		'in_port(1),eth(),eth_type(0x0806),arp()' '2' || return 1
	ovs_add_flow "test_nat_connect_v4" nat4 \
		'in_port(2),eth(),eth_type(0x0806),arp()' '1' || return 1
	ovs_add_flow "test_nat_connect_v4" nat4 \
		"ct_state(-trk),in_port(1),eth(),eth_type(0x0800),ipv4(dst=192.168.0.20)" \
		"ct(commit,nat(dst=172.31.110.20)),recirc(0x1)"
	ovs_add_flow "test_nat_connect_v4" nat4 \
		"ct_state(-trk),in_port(2),eth(),eth_type(0x0800),ipv4()" \
		"ct(commit,nat),recirc(0x2)"

	ovs_add_flow "test_nat_connect_v4" nat4 \
		"recirc_id(0x1),ct_state(+trk-inv),in_port(1),eth(),eth_type(0x0800),ipv4()" "2"
	ovs_add_flow "test_nat_connect_v4" nat4 \
		"recirc_id(0x2),ct_state(+trk-inv),in_port(2),eth(),eth_type(0x0800),ipv4()" "1"

	# do a ping
	ovs_sbx "test_nat_connect_v4" ip netns exec client ping 192.168.0.20 -c 3 || return 1

	# create an echo server in 'server'
	echo "server" | \
		ovs_netns_spawn_daemon "test_nat_connect_v4" "server" \
				nc -lvnp 4443
	ovs_sbx "test_nat_connect_v4" ip netns exec client nc -i 1 -zv 192.168.0.20 4443 || return 1

	# Now test in the other direction (should fail)
	echo "client" | \
		ovs_netns_spawn_daemon "test_nat_connect_v4" "client" \
				nc -lvnp 4443
	ovs_sbx "test_nat_connect_v4" ip netns exec client nc -i 1 -zv 172.31.110.10 4443
	if [ $? == 0 ]; then
	   info "connect to client was successful"
	   return 1
	fi

	info "done..."
	return 0
}

# netlink_validation
# - Create a dp
# - check no warning with "old version" simulation
test_netlink_checks () {
	sbx_add "test_netlink_checks" || return 1

	info "setting up new DP"
	ovs_add_dp "test_netlink_checks" nv0 || return 1
	# now try again
	PRE_TEST=$(dmesg | grep -E "RIP: [0-9a-fA-Fx]+:ovs_dp_cmd_new\+")
	ovs_add_dp "test_netlink_checks" nv0 -V 0 || return 1
	POST_TEST=$(dmesg | grep -E "RIP: [0-9a-fA-Fx]+:ovs_dp_cmd_new\+")
	if [ "$PRE_TEST" != "$POST_TEST" ]; then
		info "failed - gen warning"
		return 1
	fi

	ovs_add_netns_and_veths "test_netlink_checks" nv0 left left0 l0 || \
	    return 1
	ovs_add_netns_and_veths "test_netlink_checks" nv0 right right0 r0 || \
	    return 1
	[ $(python3 $ovs_base/ovs-dpctl.py show nv0 | grep port | \
	    wc -l) == 3 ] || \
	      return 1
	ovs_del_if "test_netlink_checks" nv0 right0 || return 1
	[ $(python3 $ovs_base/ovs-dpctl.py show nv0 | grep port | \
	    wc -l) == 2 ] || \
	      return 1

	ERR_MSG="Flow actions may not be safe on all matching packets"
	PRE_TEST=$(dmesg | grep -c "${ERR_MSG}")
	ovs_add_flow "test_netlink_checks" nv0 \
		'in_port(1),eth(),eth_type(0x0806),arp()' 'drop(0),2' \
		&> /dev/null && return 1
	POST_TEST=$(dmesg | grep -c "${ERR_MSG}")
	if [ "$PRE_TEST" == "$POST_TEST" ]; then
		info "failed - error not generated"
		return 1
	fi
	return 0
}

test_upcall_interfaces() {
	sbx_add "test_upcall_interfaces" || return 1

	info "setting up new DP"
	ovs_add_dp "test_upcall_interfaces" ui0 -V 2:1 || return 1

	ovs_add_netns_and_veths "test_upcall_interfaces" ui0 upc left0 l0 \
	    172.31.110.1/24 -u || return 1

	sleep 1
	info "sending arping"
	ip netns exec upc arping -I l0 172.31.110.20 -c 1 \
	    >$ovs_dir/arping.stdout 2>$ovs_dir/arping.stderr

	grep -E "MISS upcall\[0/yes\]: .*arp\(sip=172.31.110.1,tip=172.31.110.20,op=1,sha=" $ovs_dir/left0.out >/dev/null 2>&1 || return 1
	return 0
}

run_test() {
	(
	tname="$1"
	tdesc="$2"

	if ! lsmod | grep openvswitch >/dev/null 2>&1; then
		stdbuf -o0 printf "TEST: %-60s  [NOMOD]\n" "${tdesc}"
		return $ksft_skip
	fi

	if python3 ovs-dpctl.py -h 2>&1 | \
	     grep "Need to install the python" >/dev/null 2>&1; then
		stdbuf -o0 printf "TEST: %-60s  [PYLIB]\n" "${tdesc}"
		return $ksft_skip
	fi
	printf "TEST: %-60s  [START]\n" "${tname}"

	unset IFS

	eval test_${tname}
	ret=$?

	if [ $ret -eq 0 ]; then
		printf "TEST: %-60s  [ OK ]\n" "${tdesc}"
		ovs_exit_sig
		rm -rf "$ovs_dir"
	elif [ $ret -eq 1 ]; then
		printf "TEST: %-60s  [FAIL]\n" "${tdesc}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
			echo
			echo "Pausing. Logs in $ovs_dir/. Hit enter to continue"
			read a
		fi
		ovs_exit_sig
		[ "${PAUSE_ON_FAIL}" = "yes" ] || rm -rf "$ovs_dir"
		exit 1
	elif [ $ret -eq $ksft_skip ]; then
		printf "TEST: %-60s  [SKIP]\n" "${tdesc}"
	elif [ $ret -eq 2 ]; then
		rm -rf test_${tname}
		run_test "$1" "$2"
	fi

	return $ret
	)
	ret=$?
	case $ret in
		0)
			[ $all_skipped = true ] && [ $exitcode=$ksft_skip ] && exitcode=0
			all_skipped=false
		;;
		$ksft_skip)
			[ $all_skipped = true ] && exitcode=$ksft_skip
		;;
		*)
			all_skipped=false
			exitcode=1
		;;
	esac

	return $ret
}


exitcode=0
desc=0
all_skipped=true

while getopts :pvt o
do
	case $o in
	p) PAUSE_ON_FAIL=yes;;
	v) VERBOSE=1;;
	t) if which tcpdump > /dev/null 2>&1; then
		TRACING=1
	   else
		echo "=== tcpdump not available, tracing disabled"
	   fi
	   ;;
	*) usage;;
	esac
done
shift $(($OPTIND-1))

IFS="	
"

for arg do
	# Check first that all requested tests are available before running any
	command -v > /dev/null "test_${arg}" || { echo "=== Test ${arg} not found"; usage; }
done

name=""
desc=""
for t in ${tests}; do
	[ "${name}" = "" ]	&& name="${t}"	&& continue
	[ "${desc}" = "" ]	&& desc="${t}"

	run_this=1
	for arg do
		[ "${arg}" != "${arg#--*}" ] && continue
		[ "${arg}" = "${name}" ] && run_this=1 && break
		run_this=0
	done
	if [ $run_this -eq 1 ]; then
		run_test "${name}" "${desc}"
	fi
	name=""
	desc=""
done

exit ${exitcode}
