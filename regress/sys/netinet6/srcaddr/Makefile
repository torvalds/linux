# $OpenBSD: Makefile,v 1.2 2020/12/16 22:59:55 bluhm Exp $

RTABLE		?=	1
VETHER		?=	vether1
PROG		=	srcaddr
VETHER_LL	=	${VETHER}_ll
REXEC		=	route -T ${RTABLE} exec

CLEANFILES	+=	${VETHER_LL}

.if ! (make(clean) || make(cleandir) || make(obj))
INTERFACES != sh -c "ifconfig ${VETHER} 2>/dev/null" || true
.endif

.if ! empty(INTERFACES)
regress:
	@echo 'vether(4) interfaces already present'
	@echo SKIPPED
.endif

REGRESS_SETUP +=	setup
setup: cleanup
	${SUDO} ifconfig ${VETHER} inet6 rdomain ${RTABLE} eui64 up
	sh -c "ifconfig ${VETHER} | awk '/inet6/ {print \$$2}' > ${VETHER_LL}"
	${SUDO} ifconfig lo${RTABLE} inet rdomain ${RTABLE} 127.0.0.1/8 up
	${SUDO} ifconfig lo${RTABLE} inet6 rdomain ${RTABLE} eui64

REGRESS_CLEANUP +=	cleanup
cleanup:
.for iface in ${VETHER} lo${RTABLE}
	${SUDO} ifconfig ${iface} destroy 2>/dev/null || true
.endfor

REGRESS_SETUP +=	${PROG}

REGRESS_TARGETS +=	run-localhost
run-localhost:
	${REXEC} ./srcaddr ::1 | \
	    diff -up ${.CURDIR}/localhost.ok /dev/stdin

REGRESS_TARGETS +=	run-linklocal
run-linklocal:
	${REXEC} ./srcaddr fe80::1%${VETHER} | \
	    diff -up ${VETHER_LL} /dev/stdin

.include <bsd.regress.mk>
