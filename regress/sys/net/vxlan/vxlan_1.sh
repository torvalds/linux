#!/bin/ksh
#	$OpenBSD: vxlan_1.sh,v 1.2 2023/10/19 18:36:41 anton Exp $


cleanup()
{
	for if in $ALL_IFS; do
		ifconfig $if destroy 2>/dev/null
	done
	for id in $RDOMAINS; do
		ifconfig lo$id destroy 2>/dev/null
	done
}

CURDIR=$(cd $(dirname $0); pwd)

. ${CURDIR}/vxlan_subr

# rdomains
set -- $RDOMAINS
if [ $# -lt 2 ]; then
	echo "2 rdomain(-R option) is required" >&2
	exit 64
fi
RD1=$1
RD2=$2

# interface minor numbers
set -- $IFACE_NUMS
if [ $# -lt 2 ]; then
	echo "2 interface numbers(-I option) is required" >&2
	exit 64
fi
IFNO1=$1
IFNO2=$2

ALL_IFS="bridge$IFNO2 bridge$IFNO1 vether$IFNO2 vether$IFNO1 vxlan$IFNO2
    vxlan$IFNO1 pair$IFNO2 pair$IFNO1"

[ $CLEANUP -gt 0 ] && cleanup
#
# Check pre-conditions
#
# interfaces are busy?
for if in $ALL_IFS; do
	if iface_exists $if; then
		echo "Aborted.  interface \`$if' is used already." >&2
		exit 255
	fi
done
# rdomains are busy?
for rt in $RD1 $RD2; do
	if ! rdomain_is_used $rt; then
		echo "Aborted.  rdomain \`$rt' is used already." >&2
		exit 255
	fi
done

#
# Prepeare the test
#
[ $VERBOSE -gt 0 ] && set -x
ifconfig pair$IFNO1    rdomain $RD1 172.31.0.1/24
ifconfig pair$IFNO2    rdomain $RD2 172.31.0.2/24 patch pair$IFNO1
ifconfig vether$IFNO1  rdomain $RD1 192.168.0.1
ifconfig vether$IFNO2  rdomain $RD2 192.168.0.2
ifconfig vxlan$IFNO1 rdomain $RD1 tunneldomain $RD1 || abort_test
ifconfig vxlan$IFNO2 rdomain $RD2 tunneldomain $RD2 || abort_test
ifconfig bridge$IFNO1  rdomain $RD1 add vether$IFNO1 add vxlan$IFNO1 up
ifconfig bridge$IFNO2  rdomain $RD2 add vether$IFNO2 add vxlan$IFNO2 up

#
# Test config
#
ifconfig vxlan$IFNO1 tunnel 172.31.0.1 172.31.0.2 vnetid 100 up || abort_test
ifconfig vxlan$IFNO2 tunnel 172.31.0.2 172.31.0.1 vnetid 100 up || abort_test

#
# Test behavior
#
test ping -w 1 -c 1 -V $RD1 192.168.0.2
test ping -w 1 -c 1 -V $RD2 192.168.0.1
set +x

# Done
cleanup
exit $FAILS
