# $OpenBSD: Makefile,v 1.3 2025/06/02 15:35:00 bluhm Exp $

PROG=		tcpthread
LDADD=		-lpthread
DPADD=		${LIBPTHREAD}
WARNINGS=	yes

CLEANFILES=	ktrace.out

REGRESS_SETUP +=	${PROG}

REGRESS_TARGETS +=	run-default
run-default:
	time ${KTRACE} ./${PROG}

# Calculate number of sockets depending on mbuf cluster limit.  Each socket 
# has two socket buffers with 2 MB maximum and each cluster has 2 KB.
# sockets = maxclusters * 2 * 1024 / 2 * 2 * 1024 * 1204
# With this limit also smaller machines cannot run out of memory.
MAXSOCKS !!=	/bin/ksh -c ' \
		(( maxclusters = $$(sysctl -n kern.maxclusters) )) && \
		(( socks = maxclusters / 1024 / 2 )) && \
		(( maxsocks = socks > 100 ? 100 : socks )) && \
		echo $$maxsocks \
		'

REGRESS_TARGETS +=	run-sock100
run-sock100:
	ulimit -n 500; time ${KTRACE} ./${PROG} -n${MAXSOCKS}

REGRESS_TARGETS +=	run-splice
run-splice:
	time ${KTRACE} ./${PROG} -n10 -S2

REGRESS_TARGETS +=	run-unsplice
run-unsplice:
	time ${KTRACE} ./${PROG} -n10 -S2 -U1

REGRESS_TARGETS +=	run-max
run-max:
	time ${KTRACE} ./${PROG} -n10 -S4 -M100 -s2 -r2

REGRESS_TARGETS +=	run-idle
run-idle:
	time ${KTRACE} ./${PROG} -n10 -S4 -I100 -s2 -r2

REGRESS_ROOT_TARGETS +=	run-drop
REGRESS_TARGETS +=	run-drop
run-drop:
	time ${SUDO} ${KTRACE} ./${PROG} -n10 -D3 -o0

REGRESS_ROOT_TARGETS +=	run-splicedrop
REGRESS_TARGETS +=	run-splicedrop
run-splicedrop:
	time ${SUDO} ${KTRACE} ./${PROG} -n10 -S2 -D3 -o0

.include <bsd.regress.mk>
