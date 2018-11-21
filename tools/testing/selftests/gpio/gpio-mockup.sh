#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

#exit status
#1: Internal error
#2: sysfs/debugfs not mount
#3: insert module fail when gpio-mockup is a module.
#4: Skip test including run as non-root user.
#5: other reason.

SYSFS=
GPIO_SYSFS=
GPIO_DRV_SYSFS=
DEBUGFS=
GPIO_DEBUGFS=
dev_type=
module=

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

usage()
{
	echo "Usage:"
	echo "$0 [-f] [-m name] [-t type]"
	echo "-f:  full test. It maybe conflict with existence gpio device."
	echo "-m:  module name, default name is gpio-mockup. It could also test"
	echo "     other gpio device."
	echo "-t:  interface type: chardev(char device) and sysfs(being"
	echo "     deprecated). The first one is default"
	echo ""
	echo "$0 -h"
	echo "This usage"
}

prerequisite()
{
	msg="skip all tests:"
	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		exit $ksft_skip
	fi
	SYSFS=`mount -t sysfs | head -1 | awk '{ print $3 }'`
	if [ ! -d "$SYSFS" ]; then
		echo $msg sysfs is not mounted >&2
		exit 2
	fi
	GPIO_SYSFS=`echo $SYSFS/class/gpio`
	GPIO_DRV_SYSFS=`echo $SYSFS/devices/platform/$module/gpio`
	DEBUGFS=`mount -t debugfs | head -1 | awk '{ print $3 }'`
	if [ ! -d "$DEBUGFS" ]; then
		echo $msg debugfs is not mounted >&2
		exit 2
	fi
	GPIO_DEBUGFS=`echo $DEBUGFS/gpio`
	source gpio-mockup-sysfs.sh
}

try_insert_module()
{
	if [ -d "$GPIO_DRV_SYSFS" ]; then
		echo "$GPIO_DRV_SYSFS exist. Skip insert module"
	else
		modprobe -q $module $1
		if [ X$? != X0 ]; then
			echo $msg insmod $module failed >&2
			exit 3
		fi
	fi
}

remove_module()
{
	modprobe -r -q $module
}

die()
{
	remove_module
	exit 5
}

test_chips()
{
	if [ X$dev_type = Xsysfs ]; then
		echo "WARNING: sysfs ABI of gpio is going to deprecated."
		test_chips_sysfs $*
	else
		$BASE/gpio-mockup-chardev $*
	fi
}

gpio_test()
{
	param=$1
	valid=$2

	if [ X"$param" = X ]; then
		die
	fi
	try_insert_module "gpio_mockup_ranges=$param"
	echo -n "GPIO $module test with ranges: <"
	echo "$param>: "
	printf "%-10s %s\n" $param
	test_chips $module $valid
	remove_module
}

BASE=`dirname $0`

dev_type=
TEMP=`getopt -o fhm:t: -n '$0' -- "$@"`

if [ "$?" != "0" ]; then
        echo "Parameter process failed, Terminating..." >&2
        exit 1
fi

# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

while true; do
	case $1 in
	-f)
		full_test=true
		shift
		;;
	-h)
		usage
		exit
		;;
	-m)
		module=$2
		shift 2
		;;
	-t)
		dev_type=$2
		shift 2
		;;
	--)
		shift
		break
		;;
	*)
		echo "Internal error!"
		exit 1
		;;
	esac
done

if [ X"$module" = X ]; then
	module="gpio-mockup"
fi

if [ X$dev_type != Xsysfs ]; then
	dev_type="chardev"
fi

prerequisite

echo "1.  Test dynamic allocation of gpio successful means insert gpiochip and"
echo "    manipulate gpio pin successful"
gpio_test "-1,32" true
gpio_test "-1,32,-1,32" true
gpio_test "-1,32,-1,32,-1,32" true
if [ X$full_test = Xtrue ]; then
	gpio_test "-1,32,32,64" true
	gpio_test "-1,32,40,64,-1,5" true
	gpio_test "-1,32,32,64,-1,32" true
	gpio_test "0,32,32,64,-1,32,-1,32" true
	gpio_test "-1,32,-1,32,0,32,32,64" true
	echo "2.  Do basic test: successful means insert gpiochip and"
	echo "    manipulate gpio pin successful"
	gpio_test "0,32" true
	gpio_test "0,32,32,64" true
	gpio_test "0,32,40,64,64,96" true
fi
echo "3.  Error test: successful means insert gpiochip failed"
echo "3.1 Test number of gpio overflow"
#Currently: The max number of gpio(1024) is defined in arm architecture.
gpio_test "-1,32,-1,1024" false
if [ X$full_test = Xtrue ]; then
	echo "3.2 Test zero line of gpio"
	gpio_test "0,0" false
	echo "3.3 Test range overlap"
	echo "3.3.1 Test corner case"
	gpio_test "0,32,0,1" false
	gpio_test "0,32,32,64,32,40" false
	gpio_test "0,32,35,64,35,45" false
	gpio_test "0,32,31,32" false
	gpio_test "0,32,32,64,36,37" false
	gpio_test "0,32,35,64,34,36" false
	echo "3.3.2 Test inserting invalid second gpiochip"
	gpio_test "0,32,30,35" false
	gpio_test "0,32,1,5" false
	gpio_test "10,32,9,14" false
	gpio_test "10,32,30,35" false
	echo "3.3.3 Test others"
	gpio_test "0,32,40,56,39,45" false
	gpio_test "0,32,40,56,30,33" false
	gpio_test "0,32,40,56,30,41" false
	gpio_test "0,32,40,56,20,21" false
fi

echo GPIO test PASS

