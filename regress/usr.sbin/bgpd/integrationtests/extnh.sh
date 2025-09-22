#!/bin/ksh
#	$OpenBSD: extnh.sh,v 1.1 2025/01/13 14:18:07 claudio Exp $

set -e

BGPD=$1
BGPDCONFIGDIR=$2
RDOMAIN1=$3
RDOMAIN2=$4
PAIR1=$5
PAIR2=$6

RDOMAINS="${RDOMAIN1} ${RDOMAIN2}"
PAIRS="${PAIR1} ${PAIR2}"
PAIR1IP6=2001:db8:57::1
PAIR2IP6=2001:db8:57::2
PAIR2IP6_2=2001:db8:57::3

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
ifconfig ${PAIR1} rdomain ${RDOMAIN1} up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} up
ifconfig ${PAIR1} inet6 ${PAIR1IP6}/64
ifconfig ${PAIR2} inet6 ${PAIR2IP6}/64
ifconfig ${PAIR2} inet6 ${PAIR2IP6_2}/128
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

echo run bgpds
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.extnh.rdomain1.conf
sleep 2
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.extnh.rdomain2_1.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.extnh.rdomain2_2.conf
sleep 5

echo test
echo "router 1 in rdomain ${RDOMAIN1}" > extnh.test1.out
route -T ${RDOMAIN1} exec bgpctl show rib | tee -a extnh.test1.out
echo "router 2_1 in rdomain ${RDOMAIN2}" >> extnh.test1.out
route -T ${RDOMAIN2} exec bgpctl show rib | tee -a extnh.test1.out

echo check results
diff -u ${BGPDCONFIGDIR}/extnh.test1.ok extnh.test1.out
echo OK

exit 0
