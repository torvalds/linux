#!/bin/ksh
#	$OpenBSD: ixp.sh,v 1.3 2024/10/28 12:11:05 claudio Exp $

set -e

BGPD=$1
BGPDCONFIGDIR=$2
RDOMAIN1=$3
RDOMAIN2=$4
PAIR1=$5
PAIR2=$6

RDOMAINS="${RDOMAIN1} ${RDOMAIN2}"
PAIRS="${PAIR1} ${PAIR2}"
PAIR1IP=192.0.2.2
PAIR2IP=192.0.2.11
PAIR2IP2=192.0.2.21
PAIR2IP3=192.0.2.31
PAIR2IP4=192.0.2.41

error_notify() {
	echo cleanup
	pkill -T ${RDOMAIN1} bgpd || true
	pkill -T ${RDOMAIN2} bgpd || true
	sleep 1
	ifconfig ${PAIR2} destroy || true
	ifconfig ${PAIR1} destroy || true
	route -qn -T ${RDOMAIN1} flush || true
	route -qn -T ${RDOMAIN2} flush || true
	ifconfig lo${RDOMAIN1} destroy || true
	ifconfig lo${RDOMAIN2} destroy || true
	if [ $1 -ne 0 ]; then
		echo FAILED
		exit 1
	else
		echo SUCCESS
	fi
}

if [ "$(id -u)" -ne 0 ]; then 
	echo need root privileges >&2
	exit 1
fi

. "${BGPDCONFIGDIR}/util.sh"

trap 'error_notify $?' EXIT

echo check if rdomains are busy
for n in ${RDOMAINS}; do
	if /sbin/ifconfig | grep -v "^lo${n}:" | grep " rdomain ${n} "; then
		echo routing domain ${n} is already used >&2
		exit 1
	fi
done

echo check if interfaces are busy
for n in ${PAIRS}; do
	/sbin/ifconfig "${n}" >/dev/null 2>&1 && \
	    ( echo interface ${n} is already used >&2; exit 1 )
done

set -x

echo setup
ifconfig ${PAIR1} rdomain ${RDOMAIN1} ${PAIR1IP}/24 up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} ${PAIR2IP}/24 up
ifconfig ${PAIR2} alias ${PAIR2IP2}/32
ifconfig ${PAIR2} alias ${PAIR2IP3}/32
ifconfig ${PAIR2} alias ${PAIR2IP4}/32
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

echo run bgpds
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ixp.rdomain1.conf
sleep 2
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ixp.rdomain2_1.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ixp.rdomain2_2.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ixp.rdomain2_3.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ixp.rdomain2_4.conf

wait_until "route -T ${RDOMAIN1} exec bgpctl show rib detail | ! cmp /dev/null -"
route -T ${RDOMAIN1} exec bgpctl show rib detail | grep -v 'Last update:' | \
	tee ixp.rdomain1.out
diff -u ${BGPDCONFIGDIR}/ixp.rdomain1.ok ixp.rdomain1.out
echo OK

wait_until "route -T ${RDOMAIN2} exec bgpctl show rib detail | ! cmp /dev/null -"
route -T ${RDOMAIN2} exec bgpctl show rib detail | grep -v 'Last update:' | \
	tee ixp.rdomain2.out
diff -u ${BGPDCONFIGDIR}/ixp.rdomain2.ok ixp.rdomain2.out
echo OK

exit 0
