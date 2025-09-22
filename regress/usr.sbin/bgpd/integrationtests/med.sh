#!/bin/ksh
#	$OpenBSD: med.sh,v 1.2 2021/01/13 10:15:08 claudio Exp $

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
PAIR2IP3=10.12.57.4
PAIR2IP4=10.12.57.5

error_notify() {
	echo cleanup
	pkill -T ${RDOMAIN1} bgpd || true
	pkill -T ${RDOMAIN2} -f exabgp || true
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

run_exabgp() {
	local _t=$1

	shift
	env	exabgp.log.destination=stdout \
		exabgp.log.packets=true \
		exabgp.log.parser=true \
		exabgp.log.level=DEBUG \
		exabgp.api.cli=false \
		exabgp.daemon.user=build \
	    route -T ${RDOMAIN2} exec exabgp -1 ${1+"$@"} > ./exabgp.$_t.log
}

exacmd() {
	echo "${1+"$@"}" > med.fifo
	sleep .1	# give exabgp a bit of time
}

if [ ! -x /usr/local/bin/exabgp ]; then 
	echo install exabgp from ports for this test >&2
	exit 1
fi

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
ifconfig ${PAIR2} alias ${PAIR2IP3}/32
ifconfig ${PAIR2} alias ${PAIR2IP4}/32
ifconfig ${PAIR1} patch ${PAIR2}
ifconfig lo${RDOMAIN1} inet 127.0.0.1/8
ifconfig lo${RDOMAIN2} inet 127.0.0.1/8

[ -p med.fifo ] || mkfifo med.fifo

echo run bgpd
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.med.conf

sleep 1

echo run exabgp
run_exabgp med exabgp.med.conf &
sleep 2

echo test 1

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.1.0/24 next-hop self as-path [ 64501 64510 ] med 100'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.1.0/24 next-hop self as-path [ 64502 64510 ] med 100'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.4 announce route 10.12.1.0/24 next-hop self as-path [ 64501 64510 ] med 50'

sleep 5
route -T ${RDOMAIN1} exec bgpctl sh rib | tee med.out
sleep .2
diff -u ${BGPDCONFIGDIR}/exabgp.med.ok med.out
echo OK

exacmd 'clear adj-rib out all peers'

echo test 2

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.4 announce route 10.12.1.0/24 next-hop self as-path [ 64501 64510 ] med 50'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.1.0/24 next-hop self as-path [ 64502 64510 ] med 100'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.1.0/24 next-hop self as-path [ 64501 64510 ] med 100'

sleep 5
route -T ${RDOMAIN1} exec bgpctl sh rib | tee med.out
sleep .2
diff -u ${BGPDCONFIGDIR}/exabgp.med.ok med.out
echo OK

exacmd 'clear adj-rib out all peers'

echo test 3

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.1.0/24 next-hop self as-path [ 64501 64510 ] med 100'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.4 announce route 10.12.1.0/24 next-hop self as-path [ 64501 64510 ] med 50'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.1.0/24 next-hop self as-path [ 64502 64510 ] med 100'

sleep 5
route -T ${RDOMAIN1} exec bgpctl sh rib | tee med.out
sleep .2
diff -u ${BGPDCONFIGDIR}/exabgp.med.ok med.out
echo OK

echo test 4

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.4 withdraw route 10.12.1.0/24'

sleep 5
route -T ${RDOMAIN1} exec bgpctl sh rib | tee med.out
sleep .2
diff -u ${BGPDCONFIGDIR}/exabgp.med_2.ok med.out
echo OK
exacmd 'shutdown'

exit 0
