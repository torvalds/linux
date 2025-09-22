#!/bin/ksh
#	$OpenBSD: lladdr.sh,v 1.4 2025/04/29 18:35:51 claudio Exp $

set -e

BGPD=$1
BGPDCONFIGDIR=$2
RDOMAIN1=$3
RDOMAIN2=$4
PAIR1=$5
PAIR2=$6
GIF1=gif${RDOMAIN1}
GIF2=gif${RDOMAIN2}

RDOMAINS="${RDOMAIN1} ${RDOMAIN2}"
IFACES="${PAIR1} ${PAIR2} ${GIF1} ${GIF2}"
PAIR1IP6=fe80::c0fe:1
PAIR2IP6=fe80::c0fe:2
GIF1IP6=fe80::beef:1
GIF2IP6=fe80::beef:2

error_notify() {
	set -x
	echo cleanup
	pkill -T ${RDOMAIN1} bgpd || true
	pkill -T ${RDOMAIN2} bgpd || true
	sleep 1
	ifconfig ${GIF1} destroy || true
	ifconfig ${GIF2} destroy || true
	ifconfig ${PAIR1} destroy || true
	ifconfig ${PAIR2} destroy || true
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
for n in ${IFACES}; do
	/sbin/ifconfig "${n}" >/dev/null 2>&1 && \
	    ( echo interface ${n} is already used >&2; exit 1 )
done

set -x

echo setup
ifconfig ${PAIR1} rdomain ${RDOMAIN1} up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} up
ifconfig ${PAIR1} inet6 ${PAIR1IP6}/64
ifconfig ${PAIR2} inet6 ${PAIR2IP6}/64
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig ${GIF1} rdomain ${RDOMAIN1} tunneldomain ${RDOMAIN1}
ifconfig ${GIF2} rdomain ${RDOMAIN2} tunneldomain ${RDOMAIN2}
ifconfig ${GIF1} tunnel ${PAIR1IP6}%${PAIR1} ${PAIR2IP6}%${PAIR1}
ifconfig ${GIF2} tunnel ${PAIR2IP6}%${PAIR2} ${PAIR1IP6}%${PAIR2}
ifconfig ${GIF1} inet6 ${GIF1IP6}/128 ${GIF2IP6}
ifconfig ${GIF2} inet6 ${GIF2IP6}/128 ${GIF1IP6}

echo run bgpds
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.lladdr.rdomain1.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.lladdr.rdomain2.conf

sleep 1

route -T12 exec bgpctl nei RDOMAIN1 up
route -T12 exec bgpctl nei RDOMAIN1_2 up

sleep 2

route -T11 exec bgpctl show rib | tee lladdr.rdomain1.out
route -T11 exec bgpctl show fib | grep -v 'link#' | tee -a lladdr.rdomain1.out
route -T11 -n get 2001:db8:2::/48 | grep -v "if address" | tee -a lladdr.rdomain1.out
route -T11 -n get 2001:db8:12::/48 | grep -v "if address" | tee -a lladdr.rdomain1.out

route -T12 exec bgpctl show rib | tee lladdr.rdomain2.out
route -T12 exec bgpctl show fib | grep -v 'link#' | tee -a lladdr.rdomain2.out
route -T12 -n get 2001:db8:1::/48 | grep -v "if address" | tee -a lladdr.rdomain2.out
route -T12 -n get 2001:db8:11::/48 | grep -v "if address" | tee -a lladdr.rdomain2.out

sleep .2
diff -u ${BGPDCONFIGDIR}/lladdr.rdomain1.ok lladdr.rdomain1.out
diff -u ${BGPDCONFIGDIR}/lladdr.rdomain2.ok lladdr.rdomain2.out
echo OK

exit 0
