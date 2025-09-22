#!/bin/ksh
#	$OpenBSD: pftable.sh,v 1.1 2022/10/31 18:34:11 claudio Exp $

set -e

BGPD=$1
BGPDCONFIGDIR=$2
RDOMAIN1=$3
RDOMAIN2=$4
PAIR1=$5
PAIR2=$6

RDOMAINS="${RDOMAIN1} ${RDOMAIN2}"
PAIRS="${PAIR1} ${PAIR2}"
PAIR1IP=10.12.57.1
PAIR2IP=10.12.57.2
PAIR2IP2=10.12.57.3

error_notify() {
	echo cleanup
	pfctl -q -t bgpd_integ_test -T kill
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
ifconfig ${PAIR1} rdomain ${RDOMAIN1} ${PAIR1IP}/29 up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} ${PAIR2IP}/29 up
ifconfig ${PAIR2} alias ${PAIR2IP2}/32
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

# create an empty table
pfctl -q -t bgpd_integ_test -T add 1.1.1.1
pfctl -q -t bgpd_integ_test -T del 1.1.1.1

echo run bgpds
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.pftable.rdomain1.conf
sleep 2
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.pftable.rdomain2_1.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.pftable.rdomain2_2.conf

sleep 3

echo Check default prefixes
route -T ${RDOMAIN1} exec bgpctl show 
echo List pf table
pfctl -t bgpd_integ_test -T show
pfctl -t bgpd_integ_test -T test 10.12.62.1
pfctl -t bgpd_integ_test -T test 10.12.63.1
pfctl -t bgpd_integ_test -T test 10.12.64.1

echo Add prefix
route -T ${RDOMAIN2} exec bgpctl network add 10.12.69.0/24 
sleep 1
pfctl -t bgpd_integ_test -T test 10.12.69.1
route -T ${RDOMAIN2} exec bgpctl -s /var/run/bgpd.sock.12_2 network add 10.12.69.0/24
sleep 1
pfctl -t bgpd_integ_test -T test 10.12.69.1

echo Remove prefix
route -T ${RDOMAIN2} exec bgpctl network del 10.12.69.0/24 
sleep 1
pfctl -t bgpd_integ_test -T test 10.12.69.1
route -T ${RDOMAIN2} exec bgpctl -s /var/run/bgpd.sock.12_2 network del 10.12.69.0/24
sleep 1
! pfctl -t bgpd_integ_test -T test 10.12.69.1

exit 0
