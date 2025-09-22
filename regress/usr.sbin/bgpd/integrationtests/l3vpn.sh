#!/bin/ksh
#	$OpenBSD: l3vpn.sh,v 1.8 2025/04/29 18:35:51 claudio Exp $

set -e

BGPD=$1
BGPDCONFIGDIR=$2
RDOMAIN1=$3
RDOMAIN2=$4
PAIR1=$5
PAIR2=$6
RDOMAIN3=$7
RDOMAIN4=$8

RDOMAINS="${RDOMAIN1} ${RDOMAIN2} ${RDOMAIN3} ${RDOMAIN4}"
IFACES="${PAIR1} ${PAIR2} ${MPE1} ${MPE2}"
PAIR1IP=10.12.57.1
PAIR2IP=10.12.57.2
PAIR1IP6=2001:db8:57::1
PAIR2IP6=2001:db8:57::2

error_notify() {
	set -x
	echo cleanup
	pkill -T ${RDOMAIN1} bgpd || true
	pkill -T ${RDOMAIN2} bgpd || true
	sleep 1
	ifconfig ${PAIR1} destroy || true
	ifconfig ${PAIR2} destroy || true
	ifconfig mpe${RDOMAIN3} destroy || true
	ifconfig mpe${RDOMAIN4} destroy || true
	route -qn -T ${RDOMAIN1} flush || true
	route -qn -T ${RDOMAIN2} flush || true
	route -qn -T ${RDOMAIN3} flush || true
	route -qn -T ${RDOMAIN4} flush || true
	ifconfig lo${RDOMAIN1} destroy || true
	ifconfig lo${RDOMAIN2} destroy || true
	ifconfig lo${RDOMAIN3} destroy || true
	ifconfig lo${RDOMAIN4} destroy || true
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
for n in ${IFACES}; do
	/sbin/ifconfig "${n}" >/dev/null 2>&1 && \
	    ( echo interface ${n} is already used >&2; exit 1 )
done

set -x

echo setup
ifconfig ${PAIR1} rdomain ${RDOMAIN1} ${PAIR1IP}/29 mpls up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} ${PAIR2IP}/29 mpls up
ifconfig ${PAIR1} inet6 ${PAIR1IP6}/64
ifconfig ${PAIR2} inet6 ${PAIR2IP6}/64
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8
ifconfig mpe${RDOMAIN3} rdomain ${RDOMAIN3} mplslabel 42 192.168.237.242/32
ifconfig mpe${RDOMAIN4} rdomain ${RDOMAIN4} mplslabel 44 192.168.237.244/32
ifconfig mpe${RDOMAIN3} inet6 2001:db8:242::242/64
ifconfig mpe${RDOMAIN4} inet6 2001:db8:244::244/64
ifconfig lo${RDOMAIN3} inet 127.0.0.1/8
ifconfig lo${RDOMAIN4} inet 127.0.0.1/8

echo run bgpds
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.l3vpn.rdomain1.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.l3vpn.rdomain2.conf

sleep 1
route -T ${RDOMAIN1} exec bgpctl nei RDOMAIN2 up
route -T ${RDOMAIN1} exec bgpctl nei RDOMAIN2v6 up
sleep 1

echo Check initial networks
route -T ${RDOMAIN1} exec bgpctl show
route -T ${RDOMAIN1} exec bgpctl show rib
route -T ${RDOMAIN1} exec bgpctl show fib table 13
route -T ${RDOMAIN3} -n show
route -T ${RDOMAIN3} -n get 192.168.44/24 > /dev/null
route -T ${RDOMAIN4} -n get 192.168.42/24 > /dev/null
route -T ${RDOMAIN3} -n get -inet6 2001:db8:42:44::/64 > /dev/null
route -T ${RDOMAIN4} -n get -inet6 2001:db8:42:42::/64 > /dev/null

echo Add new network
route -T ${RDOMAIN2} exec bgpctl network add 192.168.45.0/24 rd 4200000002:14
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:42:45::/64 rd 4200000002:14
sleep 1
route -T ${RDOMAIN3} -n get 192.168.45/24 > /dev/null
route -T ${RDOMAIN3} -n get -inet6 2001:db8:42:45::/64 > /dev/null

echo Remove new network
route -T ${RDOMAIN2} exec bgpctl network del 192.168.45.0/24 rd 4200000002:14
route -T ${RDOMAIN2} exec bgpctl network del 2001:db8:42:45::/64 rd 4200000002:14
sleep 1
route -T ${RDOMAIN1} exec bgpctl show rib
! route -T ${RDOMAIN3} -n get 192.168.45/24 > /dev/null
! route -T ${RDOMAIN3} -n get -inet6 2001:db8:42:45::/64 > /dev/null

exit 0
