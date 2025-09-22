#!/bin/ksh
#	$OpenBSD: policy.sh,v 1.3 2023/02/15 14:19:08 claudio Exp $

set -e

BGPD=$1
BGPDCONFIGDIR=$2
RDOMAIN1=$3
RDOMAIN2=$4
PAIR1=$5
PAIR2=$6

RDOMAINS="${RDOMAIN1} ${RDOMAIN2}"
PAIRS="${PAIR1} ${PAIR2}"
PAIR1IP=10.12.57.254
PAIR2IP1=10.12.57.1
PAIR2IP2=10.12.57.2
PAIR2IP3=10.12.57.3
PAIR2IP4=10.12.57.4
PAIR2IP5=10.12.57.5

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

test_bgpd() {
	set -x

	e=$1
	shift

	route -T ${RDOMAIN1} exec ${BGPD} \
		-v -f ${BGPDCONFIGDIR}/bgpd.op.master.conf
	i=1
	for p in $@; do
		route -T ${RDOMAIN2} exec ${BGPD} -DNUM=$i -DPOLICY=$p \
			-DSOCK=\"/var/run/bgpd.sock.c$i\" \
			-v -f ${BGPDCONFIGDIR}/bgpd.op.client.conf
		i=$(($i + 1))
	done

	sleep 1
	route -T ${RDOMAIN1} exec bgpctl nei group TEST up
	sleep 1

	for i in 1 2 3 4 5; do
		route -T ${RDOMAIN1} exec bgpctl show nei PEER$i | \
		    grep "$e"
	done

	pkill -T ${RDOMAIN1} bgpd || true
	pkill -T ${RDOMAIN2} bgpd || true

	sleep 1
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
ifconfig ${PAIR1} rdomain ${RDOMAIN1} ${PAIR1IP}/24 up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} ${PAIR2IP1}/24 up
ifconfig ${PAIR2} alias ${PAIR2IP2}/32 up
ifconfig ${PAIR2} alias ${PAIR2IP3}/32 up
ifconfig ${PAIR2} alias ${PAIR2IP4}/32 up
ifconfig ${PAIR2} alias ${PAIR2IP5}/32 up
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

echo test1: no policy
test_bgpd "Last error sent: error in OPEN message, role mismatch" \
    "none" "none" "none" "none" "none"

echo test2: wrong policy
test_bgpd "Last error sent: error in OPEN message, role mismatch" \
    "rs" "provider" "customer" "rs" "rs-client"

echo test3: correct policy
test_bgpd "BGP state = Established, up" \
    "peer" "rs-client" "rs" "customer" "provider"
