#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later

SYSFS=
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4
retval=0
skipmsg="skip all tests:"

PCIEPORTTYPE="PCIe_Port_Link_Speed"

prerequisite()
{
	local ports

	if [ $UID != 0 ]; then
		echo $skipmsg must be run as root >&2
		exit $ksft_skip
	fi

	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`

	if [ ! -d "$SYSFS" ]; then
		echo $skipmsg sysfs is not mounted >&2
		exit $ksft_skip
	fi

	if ! ls $SYSFS/class/thermal/cooling_device* > /dev/null 2>&1; then
		echo $skipmsg thermal cooling devices missing >&2
		exit $ksft_skip
	fi

	ports=`grep -e "^$PCIEPORTTYPE" $SYSFS/class/thermal/cooling_device*/type | wc -l`
	if [ $ports -eq 0 ]; then
		echo $skipmsg pcie cooling devices missing >&2
		exit $ksft_skip
	fi
}

testport=
find_pcie_port()
{
	local patt="$1"
	local pcieports
	local max
	local cur
	local delta
	local bestdelta=-1

	pcieports=`grep -l -F -e "$patt" /sys/class/thermal/cooling_device*/type`
	if [ -z "$pcieports" ]; then
		return
	fi
	pcieports=${pcieports//\/type/}
	# Find the port with the highest PCIe Link Speed
	for port in $pcieports; do
		max=`cat $port/max_state`
		cur=`cat $port/cur_state`
		delta=$((max-cur))
		if [ $delta -gt $bestdelta ]; then
			testport="$port"
			bestdelta=$delta
		fi
	done
}

sysfspcidev=
find_sysfs_pci_dev()
{
	local typefile="$1/type"
	local pcidir

	pcidir="$SYSFS/bus/pci/devices/`sed -e "s|^${PCIEPORTTYPE}_||g" $typefile`"

	if [ -r "$pcidir/current_link_speed" ]; then
		sysfspcidev="$pcidir/current_link_speed"
	fi
}

usage()
{
	echo "Usage $0 [ -d dev ]"
	echo -e "\t-d: PCIe port BDF string (e.g., 0000:00:04.0)"
}

pattern="$PCIEPORTTYPE"
parse_arguments()
{
	while getopts d:h opt; do
		case $opt in
			h)
				usage "$0"
				exit 0
				;;
			d)
				pattern="$PCIEPORTTYPE_$OPTARG"
				;;
			*)
				usage "$0"
				exit 0
				;;
		esac
	done
}

parse_arguments "$@"
prerequisite
find_pcie_port "$pattern"
if [ -z "$testport" ]; then
	echo $skipmsg "pcie cooling device not found from sysfs" >&2
	exit $ksft_skip
fi
find_sysfs_pci_dev "$testport"
if [ -z "$sysfspcidev" ]; then
	echo $skipmsg "PCIe port device not found from sysfs" >&2
	exit $ksft_skip
fi

./set_pcie_speed.sh "$testport" "$sysfspcidev"
retval=$?

exit $retval
