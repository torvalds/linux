#!/bin/bash

# This example script creates bonding network devices based on synthetic NIC
# (the virtual network adapter usually provided by Hyper-V) and the matching
# VF NIC (SRIOV virtual function). So the synthetic NIC and VF NIC can
# function as one network device, and fail over to the synthetic NIC if VF is
# down.
#
# Usage:
# - After configured vSwitch and vNIC with SRIOV, start Linux virtual
#   machine (VM)
# - Run this scripts on the VM. It will create configuration files in
#   distro specific directory.
# - Reboot the VM, so that the bonding config are enabled.
#
# The config files are DHCP by default. You may edit them if you need to change
# to Static IP or change other settings.
#

sysdir=/sys/class/net
netvsc_cls={f8615163-df3e-46c5-913f-f2d2f965ed0e}
bondcnt=0

# Detect Distro
if [ -f /etc/redhat-release ];
then
	cfgdir=/etc/sysconfig/network-scripts
	distro=redhat
elif grep -q 'Ubuntu' /etc/issue
then
	cfgdir=/etc/network
	distro=ubuntu
elif grep -q 'SUSE' /etc/issue
then
	cfgdir=/etc/sysconfig/network
	distro=suse
else
	echo "Unsupported Distro"
	exit 1
fi

echo Detected Distro: $distro, or compatible

# Get a list of ethernet names
list_eth=(`cd $sysdir && ls -d */ | cut -d/ -f1 | grep -v bond`)
eth_cnt=${#list_eth[@]}

echo List of net devices:

# Get the MAC addresses
for (( i=0; i < $eth_cnt; i++ ))
do
	list_mac[$i]=`cat $sysdir/${list_eth[$i]}/address`
	echo ${list_eth[$i]}, ${list_mac[$i]}
done

# Find NIC with matching MAC
for (( i=0; i < $eth_cnt-1; i++ ))
do
	for (( j=i+1; j < $eth_cnt; j++ ))
	do
		if [ "${list_mac[$i]}" = "${list_mac[$j]}" ]
		then
			list_match[$i]=${list_eth[$j]}
			break
		fi
	done
done

function create_eth_cfg_redhat {
	local fn=$cfgdir/ifcfg-$1

	rm -f $fn
	echo DEVICE=$1 >>$fn
	echo TYPE=Ethernet >>$fn
	echo BOOTPROTO=none >>$fn
	echo UUID=`uuidgen` >>$fn
	echo ONBOOT=yes >>$fn
	echo PEERDNS=yes >>$fn
	echo IPV6INIT=yes >>$fn
	echo MASTER=$2 >>$fn
	echo SLAVE=yes >>$fn
}

function create_eth_cfg_pri_redhat {
	create_eth_cfg_redhat $1 $2
}

function create_bond_cfg_redhat {
	local fn=$cfgdir/ifcfg-$1

	rm -f $fn
	echo DEVICE=$1 >>$fn
	echo TYPE=Bond >>$fn
	echo BOOTPROTO=dhcp >>$fn
	echo UUID=`uuidgen` >>$fn
	echo ONBOOT=yes >>$fn
	echo PEERDNS=yes >>$fn
	echo IPV6INIT=yes >>$fn
	echo BONDING_MASTER=yes >>$fn
	echo BONDING_OPTS=\"mode=active-backup miimon=100 primary=$2\" >>$fn
}

function create_eth_cfg_ubuntu {
	local fn=$cfgdir/interfaces

	echo $'\n'auto $1 >>$fn
	echo iface $1 inet manual >>$fn
	echo bond-master $2 >>$fn
}

function create_eth_cfg_pri_ubuntu {
	local fn=$cfgdir/interfaces

	create_eth_cfg_ubuntu $1 $2
	echo bond-primary $1 >>$fn
}

function create_bond_cfg_ubuntu {
	local fn=$cfgdir/interfaces

	echo $'\n'auto $1 >>$fn
	echo iface $1 inet dhcp >>$fn
	echo bond-mode active-backup >>$fn
	echo bond-miimon 100 >>$fn
	echo bond-slaves none >>$fn
}

function create_eth_cfg_suse {
        local fn=$cfgdir/ifcfg-$1

        rm -f $fn
	echo BOOTPROTO=none >>$fn
	echo STARTMODE=auto >>$fn
}

function create_eth_cfg_pri_suse {
	create_eth_cfg_suse $1
}

function create_bond_cfg_suse {
	local fn=$cfgdir/ifcfg-$1

	rm -f $fn
	echo BOOTPROTO=dhcp >>$fn
	echo STARTMODE=auto >>$fn
	echo BONDING_MASTER=yes >>$fn
	echo BONDING_SLAVE_0=$2 >>$fn
	echo BONDING_SLAVE_1=$3 >>$fn
	echo BONDING_MODULE_OPTS=\'mode=active-backup miimon=100 primary=$2\' >>$fn
}

function create_bond {
	local bondname=bond$bondcnt
	local primary
	local secondary

	local class_id1=`cat $sysdir/$1/device/class_id 2>/dev/null`
	local class_id2=`cat $sysdir/$2/device/class_id 2>/dev/null`

	if [ "$class_id1" = "$netvsc_cls" ]
	then
		primary=$2
		secondary=$1
	elif [ "$class_id2" = "$netvsc_cls" ]
	then
		primary=$1
		secondary=$2
	else
		return 0
	fi

	echo $'\nBond name:' $bondname

	echo configuring $primary
	create_eth_cfg_pri_$distro $primary $bondname

	echo configuring $secondary
	create_eth_cfg_$distro $secondary $bondname

	echo creating: $bondname with primary slave: $primary
	create_bond_cfg_$distro $bondname $primary $secondary

	let bondcnt=bondcnt+1
}

for (( i=0; i < $eth_cnt-1; i++ ))
do
        if [ -n "${list_match[$i]}" ]
        then
		create_bond ${list_eth[$i]} ${list_match[$i]}
        fi
done
