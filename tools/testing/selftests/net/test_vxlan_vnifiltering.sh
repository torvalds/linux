#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking the VXLAN vni filtering api and
# datapath.
# It simulates two hypervisors running two VMs each using four network
# six namespaces: two for the HVs, four for the VMs. Each VM is
# connected to a separate bridge. The VM's use overlapping vlans and
# hence the separate bridge domain. Each vxlan device is a collect
# metadata device with vni filtering and hence has the ability to
# terminate configured vni's only.

#  +--------------------------------+     +------------------------------------+
#  |  vm-11 netns                   |     |  vm-21 netns                       |
#  |                                |     |                                    |
#  |+------------+  +-------------+ |     |+-------------+ +----------------+  |
#  ||veth-11.10  |  |veth-11.20   | |     ||veth-21.10   | | veth-21.20     |  |
#  ||10.0.10.11/24  |10.0.20.11/24| |     ||10.0.10.21/24| | 10.0.20.21/24  |  |
#  |+------|-----+  +|------------+ |     |+-----------|-+ +---|------------+  |
#  |       |         |              |     |            |       |               |
#  |       |         |              |     |         +------------+             |
#  |      +------------+            |     |         | veth-21    |             |
#  |      | veth-11    |            |     |         |            |             |
#  |      |            |            |     |         +-----|------+             |
#  |      +-----|------+            |     |               |                    |
#  |            |                   |     |               |                    |
#  +------------|-------------------+     +---------------|--------------------+
#  +------------|-----------------------------------------|-------------------+
#  |      +-----|------+                            +-----|------+            |
#  |      |vethhv-11   |                            |vethhv-21   |            |
#  |      +----|-------+                            +-----|------+            |
#  |       +---|---+                                  +---|--+                |
#  |       |  br1  |                                  | br2  |                |
#  |       +---|---+                                  +---|--+                |
#  |       +---|----+                                 +---|--+                |
#  |       |  vxlan1|                                 |vxlan2|                |
#  |       +--|-----+                                 +--|---+                |
#  |          |                                          |                    |
#  |          |         +---------------------+          |                    |
#  |          |         |veth0                |          |                    |
#  |          +---------|172.16.0.1/24        -----------+                    |
#  |                    |2002:fee1::1/64      |                               |
#  | hv-1 netns         +--------|------------+                               |
#  +-----------------------------|--------------------------------------------+
#                                |
#  +-----------------------------|--------------------------------------------+
#  | hv-2 netns         +--------|-------------+                              |
#  |                    | veth0                |                              |
#  |             +------| 172.16.0.2/24        |---+                          |
#  |             |      | 2002:fee1::2/64      |   |                          |
#  |             |      |                      |   |                          |
#  |             |      +----------------------+   |         -                |
#  |             |                                 |                          |
#  |           +-|-------+                +--------|-+                        |
#  |           | vxlan1  |                |  vxlan2  |                        |
#  |           +----|----+                +---|------+                        |
#  |             +--|--+                    +-|---+                           |
#  |             | br1 |                    | br2 |                           |
#  |             +--|--+                    +--|--+                           |
#  |          +-----|-------+             +----|-------+                      |
#  |          | vethhv-12   |             |vethhv-22   |                      |
#  |          +------|------+             +-------|----+                      |
#  +-----------------|----------------------------|---------------------------+
#                    |                            |
#  +-----------------|-----------------+ +--------|---------------------------+
#  |         +-------|---+             | |     +--|---------+                 |
#  |         | veth-12   |             | |     |veth-22     |                 |
#  |         +-|--------|+             | |     +--|--------|+                 |
#  |           |        |              | |        |        |                  |
#  |+----------|--+ +---|-----------+  | |+-------|-----+ +|---------------+  |
#  ||veth-12.10   | |veth-12.20     |  | ||veth-22.10   | |veth-22.20      |  |
#  ||10.0.10.12/24| |10.0.20.12/24  |  | ||10.0.10.22/24| |10.0.20.22/24   |  |
#  |+-------------+ +---------------+  | |+-------------+ +----------------+  |
#  |                                   | |                                    |
#  |                                   | |                                    |
#  | vm-12 netns                       | |vm-22 netns                         |
#  +-----------------------------------+ +------------------------------------+
#
#
# This test tests the new vxlan vnifiltering api
source lib.sh
ret=0

# all tests in this script. Can be overridden with -t option
TESTS="
	vxlan_vnifilter_api
	vxlan_vnifilter_datapath
	vxlan_vnifilter_datapath_pervni
	vxlan_vnifilter_datapath_mgroup
	vxlan_vnifilter_datapath_mgroup_pervni
	vxlan_vnifilter_metadata_and_traditional_mix
"
VERBOSE=0
PAUSE_ON_FAIL=no
PAUSE=no

which ping6 > /dev/null 2>&1 && ping6=$(which ping6) || ping6=$(which ping)

log_test()
{
	local rc=$1
	local expected=$2
	local msg="$3"

	if [ ${rc} -eq ${expected} ]; then
		printf "    TEST: %-60s  [ OK ]\n" "${msg}"
		nsuccess=$((nsuccess+1))
	else
		ret=1
		nfail=$((nfail+1))
		printf "    TEST: %-60s  [FAIL]\n" "${msg}"
		if [ "${PAUSE_ON_FAIL}" = "yes" ]; then
		echo
			echo "hit enter to continue, 'q' to quit"
			read a
			[ "$a" = "q" ] && exit 1
		fi
	fi

	if [ "${PAUSE}" = "yes" ]; then
		echo
		echo "hit enter to continue, 'q' to quit"
		read a
		[ "$a" = "q" ] && exit 1
	fi
}

run_cmd()
{
	local cmd="$1"
	local out
	local stderr="2>/dev/null"

	if [ "$VERBOSE" = "1" ]; then
		printf "COMMAND: $cmd\n"
		stderr=
	fi

	out=$(eval $cmd $stderr)
	rc=$?
	if [ "$VERBOSE" = "1" -a -n "$out" ]; then
		echo "    $out"
	fi

	return $rc
}

check_hv_connectivity() {
	slowwait 5 ip netns exec $hv_1 ping -c 1 -W 1 $1 &>/dev/null
	slowwait 5 ip netns exec $hv_1 ping -c 1 -W 1 $2 &>/dev/null

	return $?
}

check_vm_connectivity() {
	slowwait 5 run_cmd "ip netns exec $vm_11 ping -c 1 -W 1 10.0.10.12"
	log_test $? 0 "VM connectivity over $1 (ipv4 default rdst)"

	slowwait 5 run_cmd "ip netns exec $vm_21 ping -c 1 -W 1 10.0.10.22"
	log_test $? 0 "VM connectivity over $1 (ipv6 default rdst)"
}

cleanup() {
	ip link del veth-hv-1 2>/dev/null || true
	ip link del vethhv-11 vethhv-12 vethhv-21 vethhv-22 2>/dev/null || true

	cleanup_ns $hv_1 $hv_2 $vm_11 $vm_21 $vm_12 $vm_22 $vm_31 $vm_32
}

trap cleanup EXIT

setup-hv-networking() {
	id=$1
	local1=$2
	mask1=$3
	local2=$4
	mask2=$5

	ip link set veth-hv-$id netns ${hv[$id]}
	ip -netns ${hv[$id]} link set veth-hv-$id name veth0
	ip -netns ${hv[$id]} addr add $local1/$mask1 dev veth0
	ip -netns ${hv[$id]} addr add $local2/$mask2 dev veth0
	ip -netns ${hv[$id]} link set veth0 up
}

# Setups a "VM" simulated by a netns an a veth pair
# example: setup-vm <hvid> <vmid> <brid> <VATTRS> <mcast_for_bum>
# VATTRS = comma separated "<vlan>-<v[46]>-<localip>-<remoteip>-<VTYPE>-<vxlandstport>"
# VTYPE = vxlan device type. "default = traditional device, metadata = metadata device
#         vnifilter = vnifiltering device,
#         vnifilterg = vnifiltering device with per vni group/remote"
# example:
#     setup-vm 1 11 1 \
#         10-v4-172.16.0.1-239.1.1.100-vnifilterg,20-v4-172.16.0.1-239.1.1.100-vnifilterg 1
#
setup-vm() {
	hvid=$1
	vmid=$2
	brid=$3
	vattrs=$4
	mcast=$5
	lastvxlandev=""

	# create bridge
	ip -netns ${hv[$hvid]} link add br$brid type bridge vlan_filtering 1 vlan_default_pvid 0 \
		mcast_snooping 0
	ip -netns ${hv[$hvid]} link set br$brid up

	# create vm namespace and interfaces and connect to hypervisor
	# namespace
	hvvethif="vethhv-$vmid"
	vmvethif="veth-$vmid"
	ip link add $hvvethif type veth peer name $vmvethif
	ip link set $hvvethif netns ${hv[$hvid]}
	ip link set $vmvethif netns ${vm[$vmid]}
	ip -netns ${hv[$hvid]} link set $hvvethif up
	ip -netns ${vm[$vmid]} link set $vmvethif up
	ip -netns ${hv[$hvid]} link set $hvvethif master br$brid

	# configure VM vlan/vni filtering on hypervisor
	for vmap in $(echo $vattrs | cut -d "," -f1- --output-delimiter=' ')
	do
	local vid=$(echo $vmap | awk -F'-' '{print ($1)}')
	local family=$(echo $vmap | awk -F'-' '{print ($2)}')
	local localip=$(echo $vmap | awk -F'-' '{print ($3)}')
	local group=$(echo $vmap | awk -F'-' '{print ($4)}')
	local vtype=$(echo $vmap | awk -F'-' '{print ($5)}')
	local port=$(echo $vmap | awk -F'-' '{print ($6)}')

	ip -netns ${vm[$vmid]} link add name $vmvethif.$vid link $vmvethif type vlan id $vid
	ip -netns ${vm[$vmid]} addr add 10.0.$vid.$vmid/24 dev $vmvethif.$vid
	ip -netns ${vm[$vmid]} link set $vmvethif.$vid up

	tid=$vid
	vxlandev="vxlan$brid"
	vxlandevflags=""

	if [[ -n $vtype && $vtype == "metadata" ]]; then
	   vxlandevflags="$vxlandevflags external"
	elif [[ -n $vtype && $vtype == "vnifilter" || $vtype == "vnifilterg" ]]; then
	   vxlandevflags="$vxlandevflags external vnifilter"
	   tid=$((vid+brid))
	else
	   vxlandevflags="$vxlandevflags id $tid"
	   vxlandev="vxlan$tid"
	fi

	if [[ -n $vtype && $vtype != "vnifilterg" ]]; then
	   if [[ -n "$group" && "$group" != "null" ]]; then
	      if [ $mcast -eq 1 ]; then
		 vxlandevflags="$vxlandevflags group $group"
	      else
		 vxlandevflags="$vxlandevflags remote $group"
	      fi
	   fi
	fi

	if [[ -n "$port" && "$port" != "default" ]]; then
	      vxlandevflags="$vxlandevflags dstport $port"
	fi

	# create vxlan device
	if [ "$vxlandev" != "$lastvxlandev" ]; then
	     ip -netns ${hv[$hvid]} link add $vxlandev type vxlan local $localip $vxlandevflags dev veth0 2>/dev/null
	     ip -netns ${hv[$hvid]} link set $vxlandev master br$brid
	     ip -netns ${hv[$hvid]} link set $vxlandev up
	     lastvxlandev=$vxlandev
	fi

	# add vlan
	bridge -netns ${hv[$hvid]} vlan add vid $vid dev $hvvethif
	bridge -netns ${hv[$hvid]} vlan add vid $vid pvid dev $vxlandev

	# Add bridge vni filter for tx
	if [[ -n $vtype && $vtype == "metadata" || $vtype == "vnifilter" || $vtype == "vnifilterg" ]]; then
	   bridge -netns ${hv[$hvid]} link set dev $vxlandev vlan_tunnel on
	   bridge -netns ${hv[$hvid]} vlan add dev $vxlandev vid $vid tunnel_info id $tid
	fi

	if [[ -n $vtype && $vtype == "metadata" ]]; then
	   bridge -netns ${hv[$hvid]} fdb add 00:00:00:00:00:00 dev $vxlandev \
								src_vni $tid vni $tid dst $group self
	elif [[ -n $vtype && $vtype == "vnifilter" ]]; then
	   # Add per vni rx filter with 'bridge vni' api
	   bridge -netns ${hv[$hvid]} vni add dev $vxlandev vni $tid
	elif [[ -n $vtype && $vtype == "vnifilterg" ]]; then
	   # Add per vni group config with 'bridge vni' api
	   if [ -n "$group" ]; then
		if [ $mcast -eq 1 ]; then
			bridge -netns ${hv[$hvid]} vni add dev $vxlandev vni $tid group $group
		else
			bridge -netns ${hv[$hvid]} vni add dev $vxlandev vni $tid remote $group
		fi
	   fi
	fi
	done
}

setup_vnifilter_api()
{
	ip link add veth-host type veth peer name veth-testns
	setup_ns testns
	ip link set veth-testns netns $testns
}

cleanup_vnifilter_api()
{
	ip link del veth-host 2>/dev/null || true
	ip netns del $testns 2>/dev/null || true
}

# tests vxlan filtering api
vxlan_vnifilter_api()
{
	hv1addr1="172.16.0.1"
	hv2addr1="172.16.0.2"
	hv1addr2="2002:fee1::1"
	hv2addr2="2002:fee1::2"
	localip="172.16.0.1"
	group="239.1.1.101"

	cleanup_vnifilter_api &>/dev/null
	setup_vnifilter_api

	# Duplicate vni test
	# create non-vnifiltering traditional vni device
	run_cmd "ip -netns $testns link add vxlan100 type vxlan id 100 local $localip dev veth-testns dstport 4789"
	log_test $? 0 "Create traditional vxlan device"

	# create vni filtering device
	run_cmd "ip -netns $testns link add vxlan-ext1 type vxlan vnifilter local $localip dev veth-testns dstport 4789"
	log_test $? 1 "Cannot create vnifilter device without external flag"

	run_cmd "ip -netns $testns link add vxlan-ext1 type vxlan external vnifilter local $localip dev veth-testns dstport 4789"
	log_test $? 0 "Creating external vxlan device with vnifilter flag"

	run_cmd "bridge -netns $testns vni add dev vxlan-ext1 vni 100"
	log_test $? 0 "Cannot set in-use vni id on vnifiltering device"

	run_cmd "bridge -netns $testns vni add dev vxlan-ext1 vni 200"
	log_test $? 0 "Set new vni id on vnifiltering device"

	run_cmd "ip -netns $testns link add vxlan-ext2 type vxlan external vnifilter local $localip dev veth-testns dstport 4789"
	log_test $? 0 "Create second external vxlan device with vnifilter flag"

	run_cmd "bridge -netns $testns vni add dev vxlan-ext2 vni 200"
	log_test $? 255 "Cannot set in-use vni id on vnifiltering device"

	run_cmd "bridge -netns $testns vni add dev vxlan-ext2 vni 300"
	log_test $? 0 "Set new vni id on vnifiltering device"

	# check in bridge vni show
	run_cmd "bridge -netns $testns vni add dev vxlan-ext2 vni 300"
	log_test $? 0 "Update vni id on vnifiltering device"

	run_cmd "bridge -netns $testns vni add dev vxlan-ext2 vni 400"
	log_test $? 0 "Add new vni id on vnifiltering device"

	# add multicast group per vni
	run_cmd "bridge -netns $testns vni add dev vxlan-ext1 vni 200 group $group"
	log_test $? 0 "Set multicast group on existing vni"

	# add multicast group per vni
	run_cmd "bridge -netns $testns vni add dev vxlan-ext2 vni 300 group $group"
	log_test $? 0 "Set multicast group on existing vni"

	# set vnifilter on an existing external vxlan device
	run_cmd "ip -netns $testns link set dev vxlan-ext1 type vxlan external vnifilter"
	log_test $? 2 "Cannot set vnifilter flag on a device"

	# change vxlan vnifilter flag
	run_cmd "ip -netns $testns link set dev vxlan-ext1 type vxlan external novnifilter"
	log_test $? 2 "Cannot unset vnifilter flag on a device"
}

# Sanity test vnifilter datapath
# vnifilter vnis inherit BUM group from
# vxlan device
vxlan_vnifilter_datapath()
{
	hv1addr1="172.16.0.1"
	hv2addr1="172.16.0.2"
	hv1addr2="2002:fee1::1"
	hv2addr2="2002:fee1::2"

	setup_ns hv_1 hv_2
	hv[1]=$hv_1
	hv[2]=$hv_2
	ip link add veth-hv-1 type veth peer name veth-hv-2
	setup-hv-networking 1 $hv1addr1 24 $hv1addr2 64 $hv2addr1 $hv2addr2
	setup-hv-networking 2 $hv2addr1 24 $hv2addr2 64 $hv1addr1 $hv1addr2

        check_hv_connectivity hv2addr1 hv2addr2

	setup_ns vm_11 vm_21 vm_12 vm_22
	vm[11]=$vm_11
	vm[21]=$vm_21
	vm[12]=$vm_12
	vm[22]=$vm_22
	setup-vm 1 11 1 10-v4-$hv1addr1-$hv2addr1-vnifilter,20-v4-$hv1addr1-$hv2addr1-vnifilter 0
	setup-vm 1 21 2 10-v6-$hv1addr2-$hv2addr2-vnifilter,20-v6-$hv1addr2-$hv2addr2-vnifilter 0

	setup-vm 2 12 1 10-v4-$hv2addr1-$hv1addr1-vnifilter,20-v4-$hv2addr1-$hv1addr1-vnifilter 0
	setup-vm 2 22 2 10-v6-$hv2addr2-$hv1addr2-vnifilter,20-v6-$hv2addr2-$hv1addr2-vnifilter 0

        check_vm_connectivity "vnifiltering vxlan"
}

# Sanity test vnifilter datapath
# with vnifilter per vni configured BUM
# group/remote
vxlan_vnifilter_datapath_pervni()
{
	hv1addr1="172.16.0.1"
	hv2addr1="172.16.0.2"
	hv1addr2="2002:fee1::1"
	hv2addr2="2002:fee1::2"

	setup_ns hv_1 hv_2
	hv[1]=$hv_1
	hv[2]=$hv_2
	ip link add veth-hv-1 type veth peer name veth-hv-2
	setup-hv-networking 1 $hv1addr1 24 $hv1addr2 64
	setup-hv-networking 2 $hv2addr1 24 $hv2addr2 64

        check_hv_connectivity hv2addr1 hv2addr2

	setup_ns vm_11 vm_21 vm_12 vm_22
	vm[11]=$vm_11
	vm[21]=$vm_21
	vm[12]=$vm_12
	vm[22]=$vm_22
	setup-vm 1 11 1 10-v4-$hv1addr1-$hv2addr1-vnifilterg,20-v4-$hv1addr1-$hv2addr1-vnifilterg 0
	setup-vm 1 21 2 10-v6-$hv1addr2-$hv2addr2-vnifilterg,20-v6-$hv1addr2-$hv2addr2-vnifilterg 0

	setup-vm 2 12 1 10-v4-$hv2addr1-$hv1addr1-vnifilterg,20-v4-$hv2addr1-$hv1addr1-vnifilterg 0
	setup-vm 2 22 2 10-v6-$hv2addr2-$hv1addr2-vnifilterg,20-v6-$hv2addr2-$hv1addr2-vnifilterg 0

        check_vm_connectivity "vnifiltering vxlan pervni remote"
}


vxlan_vnifilter_datapath_mgroup()
{
	hv1addr1="172.16.0.1"
	hv2addr1="172.16.0.2"
	hv1addr2="2002:fee1::1"
	hv2addr2="2002:fee1::2"
        group="239.1.1.100"
        group6="ff07::1"

	setup_ns hv_1 hv_2
	hv[1]=$hv_1
	hv[2]=$hv_2
	ip link add veth-hv-1 type veth peer name veth-hv-2
	setup-hv-networking 1 $hv1addr1 24 $hv1addr2 64
	setup-hv-networking 2 $hv2addr1 24 $hv2addr2 64

        check_hv_connectivity hv2addr1 hv2addr2

	setup_ns vm_11 vm_21 vm_12 vm_22
	vm[11]=$vm_11
	vm[21]=$vm_21
	vm[12]=$vm_12
	vm[22]=$vm_22
	setup-vm 1 11 1 10-v4-$hv1addr1-$group-vnifilter,20-v4-$hv1addr1-$group-vnifilter 1
	setup-vm 1 21 2 "10-v6-$hv1addr2-$group6-vnifilter,20-v6-$hv1addr2-$group6-vnifilter" 1

        setup-vm 2 12 1 10-v4-$hv2addr1-$group-vnifilter,20-v4-$hv2addr1-$group-vnifilter 1
        setup-vm 2 22 2 10-v6-$hv2addr2-$group6-vnifilter,20-v6-$hv2addr2-$group6-vnifilter 1

        check_vm_connectivity "vnifiltering vxlan mgroup"
}

vxlan_vnifilter_datapath_mgroup_pervni()
{
	hv1addr1="172.16.0.1"
	hv2addr1="172.16.0.2"
	hv1addr2="2002:fee1::1"
	hv2addr2="2002:fee1::2"
        group="239.1.1.100"
        group6="ff07::1"

	setup_ns hv_1 hv_2
	hv[1]=$hv_1
	hv[2]=$hv_2
	ip link add veth-hv-1 type veth peer name veth-hv-2
	setup-hv-networking 1 $hv1addr1 24 $hv1addr2 64
	setup-hv-networking 2 $hv2addr1 24 $hv2addr2 64

        check_hv_connectivity hv2addr1 hv2addr2

	setup_ns vm_11 vm_21 vm_12 vm_22
	vm[11]=$vm_11
	vm[21]=$vm_21
	vm[12]=$vm_12
	vm[22]=$vm_22
	setup-vm 1 11 1 10-v4-$hv1addr1-$group-vnifilterg,20-v4-$hv1addr1-$group-vnifilterg 1
	setup-vm 1 21 2 10-v6-$hv1addr2-$group6-vnifilterg,20-v6-$hv1addr2-$group6-vnifilterg 1

        setup-vm 2 12 1 10-v4-$hv2addr1-$group-vnifilterg,20-v4-$hv2addr1-$group-vnifilterg 1
        setup-vm 2 22 2 10-v6-$hv2addr2-$group6-vnifilterg,20-v6-$hv2addr2-$group6-vnifilterg 1

        check_vm_connectivity "vnifiltering vxlan pervni mgroup"
}

vxlan_vnifilter_metadata_and_traditional_mix()
{
	hv1addr1="172.16.0.1"
	hv2addr1="172.16.0.2"
	hv1addr2="2002:fee1::1"
	hv2addr2="2002:fee1::2"

	setup_ns hv_1 hv_2
	hv[1]=$hv_1
	hv[2]=$hv_2
	ip link add veth-hv-1 type veth peer name veth-hv-2
	setup-hv-networking 1 $hv1addr1 24 $hv1addr2 64
	setup-hv-networking 2 $hv2addr1 24 $hv2addr2 64

        check_hv_connectivity hv2addr1 hv2addr2

	setup_ns vm_11 vm_21 vm_31 vm_12 vm_22 vm_32
	vm[11]=$vm_11
	vm[21]=$vm_21
	vm[31]=$vm_31
	vm[12]=$vm_12
	vm[22]=$vm_22
	vm[32]=$vm_32
	setup-vm 1 11 1 10-v4-$hv1addr1-$hv2addr1-vnifilter,20-v4-$hv1addr1-$hv2addr1-vnifilter 0
	setup-vm 1 21 2 10-v6-$hv1addr2-$hv2addr2-vnifilter,20-v6-$hv1addr2-$hv2addr2-vnifilter 0
	setup-vm 1 31 3 30-v4-$hv1addr1-$hv2addr1-default-4790,40-v6-$hv1addr2-$hv2addr2-default-4790,50-v4-$hv1addr1-$hv2addr1-metadata-4791 0


	setup-vm 2 12 1 10-v4-$hv2addr1-$hv1addr1-vnifilter,20-v4-$hv2addr1-$hv1addr1-vnifilter 0
	setup-vm 2 22 2 10-v6-$hv2addr2-$hv1addr2-vnifilter,20-v6-$hv2addr2-$hv1addr2-vnifilter 0
	setup-vm 2 32 3 30-v4-$hv2addr1-$hv1addr1-default-4790,40-v6-$hv2addr2-$hv1addr2-default-4790,50-v4-$hv2addr1-$hv1addr1-metadata-4791 0

        check_vm_connectivity "vnifiltering vxlan pervni remote mix"

	# check VM connectivity over traditional/non-vxlan filtering vxlan devices
	run_cmd "ip netns exec $vm_31 ping -c 1 -W 1 10.0.30.32"
        log_test $? 0 "VM connectivity over traditional vxlan (ipv4 default rdst)"

	run_cmd "ip netns exec $vm_31 ping -c 1 -W 1 10.0.40.32"
        log_test $? 0 "VM connectivity over traditional vxlan (ipv6 default rdst)"

	run_cmd "ip netns exec $vm_31 ping -c 1 -W 1 10.0.50.32"
        log_test $? 0 "VM connectivity over metadata nonfiltering vxlan (ipv4 default rdst)"
}

while getopts :t:pP46hv o
do
	case $o in
		t) TESTS=$OPTARG;;
		p) PAUSE_ON_FAIL=yes;;
		P) PAUSE=yes;;
		v) VERBOSE=$(($VERBOSE + 1));;
		h) usage; exit 0;;
		*) usage; exit 1;;
	esac
done

# make sure we don't pause twice
[ "${PAUSE}" = "yes" ] && PAUSE_ON_FAIL=no

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip;
fi

if [ ! -x "$(command -v ip)" ]; then
	echo "SKIP: Could not run test without ip tool"
	exit $ksft_skip
fi

ip link help vxlan 2>&1 | grep -q "vnifilter"
if [ $? -ne 0 ]; then
   echo "SKIP: iproute2 too old, missing vxlan dev vnifilter setting"
   sync
   exit $ksft_skip
fi

bridge vni help 2>&1 | grep -q "Usage: bridge vni"
if [ $? -ne 0 ]; then
   echo "SKIP: iproute2 bridge lacks vxlan vnifiltering support"
   exit $ksft_skip
fi

# start clean
cleanup &> /dev/null

for t in $TESTS
do
	case $t in
	none) setup; exit 0;;
	*) $t; cleanup;;
	esac
done

if [ "$TESTS" != "none" ]; then
	printf "\nTests passed: %3d\n" ${nsuccess}
	printf "Tests failed: %3d\n"   ${nfail}
fi

exit $ret
