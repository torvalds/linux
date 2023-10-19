#! /bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2020, Google LLC. All rights reserved.
# Author: Saravana Kannan <saravanak@google.com>

function help() {
	cat << EOF
Usage: $(basename $0) [-c|-d|-m|-f] [filter options] <list of devices>

This script needs to be run on the target device once it has booted to a
shell.

The script takes as input a list of one or more device directories under
/sys/devices and then lists the probe dependency chain (suppliers and
parents) of these devices. It does a breadth first search of the dependency
chain, so the last entry in the output is close to the root of the
dependency chain.

By default it lists the full path to the devices under /sys/devices.

It also takes an optional modifier flag as the first parameter to change
what information is listed in the output. If the requested information is
not available, the device name is printed.

  -c	lists the compatible string of the dependencies
  -d	lists the driver name of the dependencies that have probed
  -m	lists the module name of the dependencies that have a module
  -f	list the firmware node path of the dependencies
  -g	list the dependencies as edges and nodes for graphviz
  -t	list the dependencies as edges for tsort

The filter options provide a way to filter out some dependencies:
  --allow-no-driver	By default dependencies that don't have a driver
			attached are ignored. This is to avoid following
			device links to "class" devices that are created
			when the consumer probes (as in, not a probe
			dependency). If you want to follow these links
			anyway, use this flag.

  --exclude-devlinks	Don't follow device links when tracking probe
			dependencies.

  --exclude-parents	Don't follow parent devices when tracking probe
			dependencies.

EOF
}

function dev_to_detail() {
	local i=0
	while [ $i -lt ${#OUT_LIST[@]} ]
	do
		local C=${OUT_LIST[i]}
		local S=${OUT_LIST[i+1]}
		local D="'$(detail_chosen $C $S)'"
		if [ ! -z "$D" ]
		then
			# This weirdness is needed to work with toybox when
			# using the -t option.
			printf '%05u\t%s\n' ${i} "$D" | tr -d \'
		fi
		i=$((i+2))
	done
}

function already_seen() {
	local i=0
	while [ $i -lt ${#OUT_LIST[@]} ]
	do
		if [ "$1" = "${OUT_LIST[$i]}" ]
		then
			# if-statement treats 0 (no-error) as true
			return 0
		fi
		i=$(($i+2))
	done

	# if-statement treats 1 (error) as false
	return 1
}

# Return 0 (no-error/true) if parent was added
function add_parent() {

	if [ ${ALLOW_PARENTS} -eq 0 ]
	then
		return 1
	fi

	local CON=$1
	# $CON could be a symlink path. So, we need to find the real path and
	# then go up one level to find the real parent.
	local PARENT=$(realpath $CON/..)

	while [ ! -e ${PARENT}/driver ]
	do
		if [ "$PARENT" = "/sys/devices" ]
		then
			return 1
		fi
		PARENT=$(realpath $PARENT/..)
	done

	CONSUMERS+=($PARENT)
	OUT_LIST+=(${CON} ${PARENT})
	return 0
}

# Return 0 (no-error/true) if one or more suppliers were added
function add_suppliers() {
	local CON=$1
	local RET=1

	if [ ${ALLOW_DEVLINKS} -eq 0 ]
	then
		return 1
	fi

	SUPPLIER_LINKS=$(ls -1d $CON/supplier:* 2>/dev/null)
	for SL in $SUPPLIER_LINKS;
	do
		SYNC_STATE=$(cat $SL/sync_state_only)

		# sync_state_only links are proxy dependencies.
		# They can also have cycles. So, don't follow them.
		if [ "$SYNC_STATE" != '0' ]
		then
			continue
		fi

		SUPPLIER=$(realpath $SL/supplier)

		if [ ! -e $SUPPLIER/driver -a ${ALLOW_NO_DRIVER} -eq 0 ]
		then
			continue
		fi

		CONSUMERS+=($SUPPLIER)
		OUT_LIST+=(${CON} ${SUPPLIER})
		RET=0
	done

	return $RET
}

function detail_compat() {
	f=$1/of_node/compatible
	if [ -e $f ]
	then
		echo -n $(cat $f)
	else
		echo -n $1
	fi
}

function detail_module() {
	f=$1/driver/module
	if [ -e $f ]
	then
		echo -n $(basename $(realpath $f))
	else
		echo -n $1
	fi
}

function detail_driver() {
	f=$1/driver
	if [ -e $f ]
	then
		echo -n $(basename $(realpath $f))
	else
		echo -n $1
	fi
}

function detail_fwnode() {
	f=$1/firmware_node
	if [ ! -e $f ]
	then
		f=$1/of_node
	fi

	if [ -e $f ]
	then
		echo -n $(realpath $f)
	else
		echo -n $1
	fi
}

function detail_graphviz() {
	if [ "$2" != "ROOT" ]
	then
		echo -n "\"$(basename $2)\"->\"$(basename $1)\""
	else
		echo -n "\"$(basename $1)\""
	fi
}

function detail_tsort() {
	echo -n "\"$2\" \"$1\""
}

function detail_device() { echo -n $1; }

alias detail=detail_device
ALLOW_NO_DRIVER=0
ALLOW_DEVLINKS=1
ALLOW_PARENTS=1

while [ $# -gt 0 ]
do
	ARG=$1
	case $ARG in
		--help)
			help
			exit 0
			;;
		-c)
			alias detail=detail_compat
			;;
		-m)
			alias detail=detail_module
			;;
		-d)
			alias detail=detail_driver
			;;
		-f)
			alias detail=detail_fwnode
			;;
		-g)
			alias detail=detail_graphviz
			;;
		-t)
			alias detail=detail_tsort
			;;
		--allow-no-driver)
			ALLOW_NO_DRIVER=1
			;;
		--exclude-devlinks)
			ALLOW_DEVLINKS=0
			;;
		--exclude-parents)
			ALLOW_PARENTS=0
			;;
		*)
			# Stop at the first argument that's not an option.
			break
			;;
	esac
	shift
done

function detail_chosen() {
	detail $1 $2
}

if [ $# -eq 0 ]
then
	help
	exit 1
fi

CONSUMERS=($@)
OUT_LIST=()

# Do a breadth first, non-recursive tracking of suppliers. The parent is also
# considered a "supplier" as a device can't probe without its parent.
i=0
while [ $i -lt ${#CONSUMERS[@]} ]
do
	CONSUMER=$(realpath ${CONSUMERS[$i]})
	i=$(($i+1))

	if already_seen ${CONSUMER}
	then
		continue
	fi

	# If this is not a device with a driver, we don't care about its
	# suppliers.
	if [ ! -e ${CONSUMER}/driver -a ${ALLOW_NO_DRIVER} -eq 0 ]
	then
		continue
	fi

	ROOT=1

	# Add suppliers to CONSUMERS list and output the consumer details.
	#
	# We don't need to worry about a cycle in the dependency chain causing
	# infinite loops. That's because the kernel doesn't allow cycles in
	# device links unless it's a sync_state_only device link. And we ignore
	# sync_state_only device links inside add_suppliers.
	if add_suppliers ${CONSUMER}
	then
		ROOT=0
	fi

	if add_parent ${CONSUMER}
	then
		ROOT=0
	fi

	if [ $ROOT -eq 1 ]
	then
		OUT_LIST+=(${CONSUMER} "ROOT")
	fi
done

# Can NOT combine sort and uniq using sort -suk2 because stable sort in toybox
# isn't really stable.
dev_to_detail | sort -k2 -k1 | uniq -f 1 | sort | cut -f2-

exit 0
