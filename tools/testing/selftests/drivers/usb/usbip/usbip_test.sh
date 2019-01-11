#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

usage() { echo "usbip_test.sh -b <busid> -p <usbip tools path>"; exit 1; }

while getopts "h:b:p:" arg; do
    case "${arg}" in
	h)
	    usage
	    ;;
	b)
	    busid=${OPTARG}
	    ;;
	p)
	    tools_path=${OPTARG}
	    ;;
	*)
	    usage
	    ;;
    esac
done
shift $((OPTIND-1))

if [ -z "${busid}" ]; then
	usage
fi

echo "Running USB over IP Testing on $busid";

test_end_msg="End of USB over IP Testing on $busid"

if [ $UID != 0 ]; then
	echo "Please run usbip_test as root [SKIP]"
	echo $test_end_msg
	exit $ksft_skip
fi

echo "Load usbip_host module"
if ! /sbin/modprobe -q -n usbip_host; then
	echo "usbip_test: module usbip_host is not found [SKIP]"
	echo $test_end_msg
	exit $ksft_skip
fi

if /sbin/modprobe -q usbip_host; then
	echo "usbip_test: module usbip_host is loaded [OK]"
else
	echo "usbip_test: module usbip_host failed to load [FAIL]"
	echo $test_end_msg
	exit 1
fi

echo "Load vhci_hcd module"
if /sbin/modprobe -q vhci_hcd; then
	echo "usbip_test: module vhci_hcd is loaded [OK]"
else
	echo "usbip_test: module vhci_hcd failed to load [FAIL]"
	echo $test_end_msg
	exit 1
fi
echo "=============================================================="

cd $tools_path;

if [ ! -f src/usbip ]; then
	echo "Please build usbip tools"
	echo $test_end_msg
	exit $ksft_skip
fi

echo "Expect to see export-able devices";
src/usbip list -l;
echo "=============================================================="

echo "Run lsusb to see all usb devices"
lsusb -t;
echo "=============================================================="

src/usbipd -D;

echo "Get exported devices from localhost - expect to see none";
src/usbip list -r localhost;
echo "=============================================================="

echo "bind devices";
src/usbip bind -b $busid;
echo "=============================================================="

echo "Run lsusb - bound devices should be under usbip_host control"
lsusb -t;
echo "=============================================================="

echo "bind devices - expect already bound messages"
src/usbip bind -b $busid;
echo "=============================================================="

echo "Get exported devices from localhost - expect to see exported devices";
src/usbip list -r localhost;
echo "=============================================================="

echo "unbind devices";
src/usbip unbind -b $busid;
echo "=============================================================="

echo "Run lsusb - bound devices should be rebound to original drivers"
lsusb -t;
echo "=============================================================="

echo "unbind devices - expect no devices bound message";
src/usbip unbind -b $busid;
echo "=============================================================="

echo "Get exported devices from localhost - expect to see none";
src/usbip list -r localhost;
echo "=============================================================="

echo "List imported devices - expect to see none";
src/usbip port;
echo "=============================================================="

echo "Import devices from localhost - should fail with no devices"
src/usbip attach -r localhost -b $busid;
echo "=============================================================="

echo "bind devices";
src/usbip bind -b $busid;
echo "=============================================================="

echo "List imported devices - expect to see exported devices";
src/usbip list -r localhost;
echo "=============================================================="

echo "List imported devices - expect to see none";
src/usbip port;
echo "=============================================================="

echo "Import devices from localhost - should work"
src/usbip attach -r localhost -b $busid;
echo "=============================================================="

# Wait for sysfs file to be updated. Without this sleep, usbip port
# shows no imported devices.
sleep 3;

echo "List imported devices - expect to see imported devices";
src/usbip port;
echo "=============================================================="

echo "Import devices from localhost - expect already imported messages"
src/usbip attach -r localhost -b $busid;
echo "=============================================================="

echo "Un-import devices";
src/usbip detach -p 00;
src/usbip detach -p 01;
echo "=============================================================="

echo "List imported devices - expect to see none";
src/usbip port;
echo "=============================================================="

echo "Un-import devices - expect no devices to detach messages";
src/usbip detach -p 00;
src/usbip detach -p 01;
echo "=============================================================="

echo "Detach invalid port tests - expect invalid port error message";
src/usbip detach -p 100;
echo "=============================================================="

echo "Expect to see export-able devices";
src/usbip list -l;
echo "=============================================================="

echo "Remove usbip_host module";
rmmod usbip_host;

echo "Run lsusb - bound devices should be rebound to original drivers"
lsusb -t;
echo "=============================================================="

echo "Run bind without usbip_host - expect fail"
src/usbip bind -b $busid;
echo "=============================================================="

echo "Run lsusb - devices that failed to bind aren't bound to any driver"
lsusb -t;
echo "=============================================================="

echo "modprobe usbip_host - does it work?"
/sbin/modprobe usbip_host
echo "Should see -busid- is not in match_busid table... skip! dmesg"
echo "=============================================================="
dmesg | grep "is not in match_busid table"
echo "=============================================================="

echo $test_end_msg
