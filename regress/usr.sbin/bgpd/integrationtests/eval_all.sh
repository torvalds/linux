#!/bin/ksh
#	$OpenBSD: eval_all.sh,v 1.3 2021/05/10 10:29:04 claudio Exp $

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
	echo "${1+"$@"}" > eval_all.fifo
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

[ -p eval_all.fifo ] || mkfifo eval_all.fifo

echo run bgpd
route -T ${RDOMAIN1} exec ${BGPD} \
	-v -f ${BGPDCONFIGDIR}/bgpd.eval_all.conf

sleep 1

echo run exabgp
run_exabgp eval_all exabgp.eval_all.conf 2>&1 &
sleep 2

# initial announcements
echo test 1

# no filtering
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.1.0/24 next-hop self'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.1.0/24 next-hop self'
# filter from 64501
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.2.0/24 next-hop self community [ 64500:64503 64500:64504 ]'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.2.0/24 next-hop self'
# filter from both
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.3.0/24 next-hop self community [ 64500:64503 64500:64504 ]'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.3.0/24 next-hop self community [ 64500:64503 64500:64504 ]'
# similar with aspath and prepends
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.0.1.0/24 next-hop self as-path [ 64501 101 ] community [ 64500:64503 64500:64504 ]'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.0.1.0/24 next-hop self as-path [ 64502 101 101 101 ]'

sleep 3
route -T ${RDOMAIN1} exec bgpctl sh rib
(route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.4 detail;
route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.5 detail ) | \
	grep -v 'Last update:' | tee eval_all.out
sleep .2
diff -u ${BGPDCONFIGDIR}/eval_all.test1.ok eval_all.out
echo OK

# withdraw hidden route
echo 'test 2 (withdraw hidden)'

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 withdraw route 10.12.2.0/24'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 withdraw route 10.0.1.0/24'

sleep 3
route -T ${RDOMAIN1} exec bgpctl sh rib
(route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.4 detail;
route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.5 detail ) | \
	grep -v 'Last update:' | tee eval_all.out
sleep .2
diff -u ${BGPDCONFIGDIR}/eval_all.test2.ok eval_all.out
echo OK

# readd route should give use same result as 1
echo 'test 3 (readd hidden)'

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.12.2.0/24 next-hop self'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.3 announce route 10.0.1.0/24 next-hop self as-path [ 64502 101 101 101 ]'

sleep 3
route -T ${RDOMAIN1} exec bgpctl sh rib
(route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.4 detail;
route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.5 detail ) | \
	grep -v 'Last update:' | tee eval_all.out
sleep .2
diff -u ${BGPDCONFIGDIR}/eval_all.test1.ok eval_all.out
echo OK

# withdraw primary route (should not change output)
echo 'test 4 (withdraw best)'

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 withdraw route 10.12.2.0/24'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 withdraw route 10.0.1.0/24'

sleep 3
route -T ${RDOMAIN1} exec bgpctl sh rib
(route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.4 detail;
route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.5 detail ) | \
	grep -v 'Last update:' | tee eval_all.out
sleep .2
diff -u ${BGPDCONFIGDIR}/eval_all.test4.ok eval_all.out
echo OK

# readd route should give use same result as 1
echo 'test 5 (readd best)'

exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.12.2.0/24 next-hop self community [ 64500:64503 64500:64504 ]'
exacmd 'neighbor 10.12.57.1 router-id 10.12.57.2 announce route 10.0.1.0/24 next-hop self as-path [ 64501 101 ] community [ 64500:64503 64500:64504 ]'

sleep 3
route -T ${RDOMAIN1} exec bgpctl sh rib
(route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.4 detail;
route -T ${RDOMAIN1} exec bgpctl sh rib out nei 10.12.57.5 detail ) | \
	grep -v 'Last update:' | tee eval_all.out
sleep .2
diff -u ${BGPDCONFIGDIR}/eval_all.test1.ok eval_all.out
echo OK

exacmd 'shutdown'
exit 0
