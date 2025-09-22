#!/bin/ksh
#	$OpenBSD: carp_4.sh,v 1.3 2024/01/05 10:37:54 anton Exp $


cleanup()
{
	for if in $ALL_IFS; do
		ifconfig $if destroy 2>/dev/null
	done
	for i in $RDOMAINS; do
		ifconfig lo$i destroy 2>/dev/null
	done
}

CURDIR=$(cd $(dirname $0); pwd)

. ${CURDIR}/carp_subr

# rdomains
set -- $RDOMAINS
if [ $# -lt 2 ]; then
	echo "2 rdomain(-R option) is required" >&2
	exit 64
fi
RD1=$1
RD2=$2
IFGPREFIX=$(printf "regress%04x" $$)

# interface minor numbers
set -- $IFACE_NUMS
if [ $# -lt 2 ]; then
	echo "2 interface numbers(-I option) is required" >&2
	exit 64
fi
IFNO1=$1
IFNO2=$2

ALL_IFS="carp$IFNO1 carp$IFNO2 pair$IFNO1 pair$IFNO2"

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
ifconfig pair$IFNO1 rdomain $RD1 192.168.0.2/24
ifconfig pair$IFNO2 rdomain $RD2 192.168.0.3/24 patch pair$IFNO1

lladdr1=$(ifconfig pair$IFNO1 | sed -n '/^.*lladdr \(.*\)/s//\1/p')
lladdr2=$(ifconfig pair$IFNO2 | sed -n '/^.*lladdr \(.*\)/s//\1/p')

#
# Test config
#
ifconfig carp$IFNO1 rdomain $RD1 lladdr $lladdr1 192.168.0.1/24 \
    vhid 251 carpdev pair$IFNO1 -group carp group ${IFGPREFIX}a \
    carppeer 192.168.0.3 || abort_test
ifconfig carp$IFNO2 rdomain $RD2 lladdr $lladdr2 192.168.0.1/24 \
    vhid 251 carpdev pair$IFNO2 -group carp group ${IFGPREFIX}b \
    advskew 100 carppeer 192.168.0.2 || abort_test

#
# Test behavior
#

# IFNO1 must become master
wait_until "ifconfig carp$IFNO1 | grep -q 'status: master'"
test sh -c "ifconfig carp$IFNO1 | grep -q 'status: master'"
test sh -c "ifconfig carp$IFNO2 | grep -q 'status: backup'"

# carpdemote must work
ifconfig -g ${IFGPREFIX}a carpdemote
wait_until "ifconfig carp$IFNO1 | grep -q 'status: backup'"
test sh -c "ifconfig carp$IFNO1 | grep -q 'status: backup'"
test sh -c "ifconfig carp$IFNO2 | grep -q 'status: master'"

# Done
cleanup
exit $FAILS
