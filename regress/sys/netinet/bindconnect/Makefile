#	$OpenBSD: Makefile,v 1.6 2024/01/26 18:09:24 bluhm Exp $

PROG=		bindconnect
LDADD=		-lpthread
DPADD=		${LIBPTHREAD}
WARNINGS=	yes

CLEANFILES=	ktrace.out

LOCAL_NET ?=
LOCAL_NET6 ?=

REGRESS_ROOT_TARGETS +=	setup-maxfiles
REGRESS_SETUP_ONCE +=	setup-maxfiles
setup-maxfiles:
	[[ $$(sysctl -n kern.maxfiles) -ge 110000 ]] || \
	    ${SUDO} sysctl kern.maxfiles=110000

REGRESS_SETUP +=	${PROG}

REGRESS_TARGETS +=	run-default
run-default:
	time ${KTRACE} ./${PROG}

NET_inet =	${LOCAL_NET}
NET_inet6 =	${LOCAL_NET6}

.for af in inet inet6

.if ! empty(NET_${af})
REGRESS_CLEANUP +=	cleanup-${af}-delete
.endif
cleanup-${af}-delete:
	-${SUDO} time ./${PROG} \
	    -f ${af} -s 0 -o 0 -b 0 -c 0 -d 1 -N ${NET_${af}} -t 1

.for proto in udp tcp any

.if "${proto}" != udp && "${proto}" != tcp
SUDO_${proto} = ${SUDO}
.else
SUDO_${proto} =
.endif

REGRESS_TARGETS +=	run-${af}-${proto}-bind
run-${af}-${proto}-bind:
	${SUDO_${proto}} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -b 6 -c 0

REGRESS_TARGETS +=	run-${af}-${proto}-connect
run-${af}-${proto}-connect:
	${SUDO_${proto}} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -b 0 -c 6

REGRESS_TARGETS +=	run-${af}-${proto}-bind-connect
run-${af}-${proto}-bind-connect:
	${SUDO_${proto}} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -b 3 -c 3

# skip test on armv7, it fails with malloc: out of space in kmem_map
.if "${MACHINE}" == armv7 || "${MACHINE}" == macppc
REGRESS_SKIP_TARGETS +=	run-${af}-${proto}-100000
.endif
REGRESS_ROOT_TARGETS +=	run-${af}-${proto}-100000
REGRESS_TARGETS +=	run-${af}-${proto}-100000
run-${af}-${proto}-100000:
	${SUDO} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 100000 -s 2 -o 1 -b 3 -c 3

REGRESS_TARGETS +=	run-${af}-${proto}-reuseport
run-${af}-${proto}-reuseport:
	${SUDO_${proto}} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -b 3 -c 3 -r

.if empty(NET_${af})
REGRESS_SKIP_TARGETS +=	run-${af}-${proto}-localnet-connect \
			run-${af}-${proto}-localnet-bind-connect \
			run-${af}-${proto}-localnet-connect-delete
.endif

REGRESS_TARGETS +=	run-${af}-${proto}-localnet-connect
run-${af}-${proto}-localnet-connect:
	${SUDO_${proto}} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -c 6 -N ${NET_${af}}

REGRESS_TARGETS +=	run-${af}-${proto}-localnet-bind-connect
run-${af}-${proto}-localnet-bind-connect:
	${SUDO_${proto}} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -b 3 -c 3 -N ${NET_${af}}

REGRESS_ROOT_TARGETS +=	run-${af}-${proto}-localnet-connect-delete
REGRESS_TARGETS +=	run-${af}-${proto}-localnet-connect-delete
run-${af}-${proto}-localnet-connect-delete:
	${SUDO} time ${KTRACE} ./${PROG} \
	    -f ${af} -p ${proto} -n 16 -s 2 -o 1 -b 0 -c 6 -d 3 -N ${NET_${af}}

.endfor
.endfor

REGRESS_ROOT_TARGETS +=	${REGRESS_TARGETS:M*-any-*}

.include <bsd.regress.mk>
