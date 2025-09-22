#!/bin/ksh
#	$OpenBSD: ovs.sh,v 1.5 2023/02/15 14:19:08 claudio Exp $

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
PAIR2STATIC=10.12.58.0/24
PAIR2CONNIP=10.12.59.1
PAIR2CONNPREF=24
PAIR2CONN=10.12.59.0/24
PAIR2RTABLE=10.12.60.0/24
PAIR2PRIORITY=10.12.61.0/24
PAIR2VALID=10.12.62.0/24
PAIR2INVALIDAS=10.12.63.0/24
PAIR2NOTFOUND=10.12.64.0/24


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
ifconfig ${PAIR1} rdomain ${RDOMAIN1} ${PAIR1IP}/30 up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} ${PAIR2IP}/30 up
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

echo add routes
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ovs.rdomain1.conf
route -T ${RDOMAIN2} add ${PAIR2STATIC} ${PAIR1IP}
ifconfig ${PAIR2} alias ${PAIR2CONNIP}/${PAIR2CONNPREF}
route -T ${RDOMAIN2} add -label PAIR2RTABLE ${PAIR2RTABLE} \
	${PAIR1IP}
route -T ${RDOMAIN2} add -priority 55 ${PAIR2PRIORITY} \
	${PAIR1IP}
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.ovs.rdomain2.conf

sleep 1
route -T ${RDOMAIN1} exec bgpctl nei RDOMAIN2 up
sleep 1

echo test 1
route -T ${RDOMAIN1} exec bgpctl sh rib ovs valid | \
	grep ${PAIR2VALID}
route -T ${RDOMAIN1} exec bgpctl sh rib ovs invalid | \
	grep ${PAIR2INVALIDAS}
route -T ${RDOMAIN1} exec bgpctl sh rib ovs not-found | \
	grep ${PAIR2NOTFOUND}

echo reload config
route -T ${RDOMAIN1} exec bgpctl reload
sleep 2

echo test 2
route -T ${RDOMAIN1} exec bgpctl sh rib ovs valid | \
	grep ${PAIR2VALID}
route -T ${RDOMAIN1} exec bgpctl sh rib ovs invalid | \
	grep ${PAIR2INVALIDAS}
route -T ${RDOMAIN1} exec bgpctl sh rib ovs not-found | \
	grep ${PAIR2NOTFOUND}

exit 0
