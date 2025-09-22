#!/bin/ksh
#	$OpenBSD: maxattr.sh,v 1.5 2024/12/09 10:53:54 claudio Exp $

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
PAIR2IP_2=10.12.57.3
PAIR2IP_3=10.12.57.4
PAIR1IP6=2001:db8:57::1
PAIR2IP6=2001:db8:57::2
PAIR2IP6_2=2001:db8:57::3
PAIR2IP6_3=2001:db8:57::4

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
ifconfig ${PAIR1} rdomain ${RDOMAIN1} ${PAIR1IP}/29 up
ifconfig ${PAIR2} rdomain ${RDOMAIN2} ${PAIR2IP}/29 up
ifconfig ${PAIR1} inet6 ${PAIR1IP6}/64
ifconfig ${PAIR2} inet6 ${PAIR2IP6}/64
ifconfig ${PAIR2} alias ${PAIR2IP_2}/32
ifconfig ${PAIR2} alias ${PAIR2IP_3}/32
ifconfig ${PAIR2} inet6 ${PAIR2IP6_2}/128
ifconfig ${PAIR2} inet6 ${PAIR2IP6_3}/128
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

echo run bgpds
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.maxattr.rdomain1.conf
sleep 2
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.maxattr.rdomain2_1.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.maxattr.rdomain2_2.conf
route -T ${RDOMAIN2} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.maxattr.rdomain2_3.conf
sleep 1

echo inject initial prefixes
route -T ${RDOMAIN2} exec bgpctl network add 10.12.60.0/24
route -T ${RDOMAIN2} exec bgpctl network add 10.12.61.0/24 community 0:1
route -T ${RDOMAIN2} exec bgpctl network add 10.12.62.0/24 community 0:1
route -T ${RDOMAIN2} exec bgpctl network add 10.12.63.0/24 community 0:1
route -T ${RDOMAIN2} exec bgpctl network add 10.12.64.0/24 community 0:1
route -T ${RDOMAIN2} exec bgpctl network add 10.12.65.0/24 community 0:1
route -T ${RDOMAIN2} exec bgpctl network add 10.12.66.0/24 community 0:1
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:60::/48
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:61::/48 community 0:2
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:62::/48 community 0:2
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:63::/48 community 0:2
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:64::/48 community 0:2
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:65::/48 community 0:2
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:66::/48 community 0:2

sleep 4
echo test1: check propagation
route -T ${RDOMAIN1} exec bgpctl show rib out | tee maxattr.test1.out
echo "regular peer" >> maxattr.test1.out
route -T ${RDOMAIN2} exec bgpctl -s /var/run/bgpd.sock.12_2 show rib | tee -a maxattr.test1.out
echo "extended message peer" >> maxattr.test1.out
route -T ${RDOMAIN2} exec bgpctl -s /var/run/bgpd.sock.12_3 show rib | tee -a maxattr.test1.out

echo update prefixes
route -T ${RDOMAIN2} exec bgpctl network add 10.12.62.0/24 community 0:1 community 42:1
route -T ${RDOMAIN2} exec bgpctl network add 10.12.63.0/24 community 0:1 community 42:2
route -T ${RDOMAIN2} exec bgpctl network add 10.12.64.0/24 community 0:1 community 42:3
route -T ${RDOMAIN2} exec bgpctl network add 10.12.65.0/24 community 0:1 community 42:4
route -T ${RDOMAIN2} exec bgpctl network add 10.12.66.0/24 community 0:1 community 42:5
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:62::/48 community 0:2 community 42:1
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:63::/48 community 0:2 community 42:2
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:64::/48 community 0:2 community 42:3
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:65::/48 community 0:2 community 42:4
route -T ${RDOMAIN2} exec bgpctl network add 2001:db8:66::/48 community 0:2 community 42:5

sleep 2
echo test2: check propagation
route -T ${RDOMAIN1} exec bgpctl show rib out | tee maxattr.test2.out
echo "regular peer" >> maxattr.test2.out
route -T ${RDOMAIN2} exec bgpctl -s /var/run/bgpd.sock.12_2 show rib | tee -a maxattr.test2.out
echo "extended message peer" >> maxattr.test2.out
route -T ${RDOMAIN2} exec bgpctl -s /var/run/bgpd.sock.12_3 show rib | tee -a maxattr.test2.out

echo check results
diff -u ${BGPDCONFIGDIR}/maxattr.test1.ok maxattr.test1.out
diff -u ${BGPDCONFIGDIR}/maxattr.test2.ok maxattr.test2.out
echo OK

exit 0
