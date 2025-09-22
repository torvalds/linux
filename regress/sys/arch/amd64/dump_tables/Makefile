# $OpenBSD: Makefile,v 1.2 2022/04/21 19:21:04 bluhm Exp $

.if "${MACHINE_ARCH}" != amd64
regress:
	# MACHINE_ARCH is not amd64
	@echo SKIPPED
.else

PROG=		dump_tables
LDADD=		-lkvm
DPADD=		${LIBKVM}
CFLAGS=		-I.
PMAP_H=		/usr/include/machine/pmap.h
SPMAP_H=	struct_pmap.h
CLEANFILES=	${SPMAP_H} tables

REGRESS_TARGETS=	run-regress-dump_tables
run-regress-dump_tables: dump_tables
	${SUDO} ./dump_tables >tables

dump_tables.o: ${SPMAP_H}

s=[[:space:]]
${SPMAP_H}: ${PMAP_H}
	sed -nE -e '/^struct$s+pmap$s*\{/,/^}/p' \
		-e '/^$s*#$s*define$s+PMAP_DIRECT_(UN)?MAP\(/p' \
	    ${PMAP_H} >$@

.if ! (make(clean) || make(cleandir) || make(obj))
ALLOWKMEM !=		/sbin/sysctl -n kern.allowkmem

.if ${ALLOWKMEM} == 0
REGRESS_SKIP_TARGETS =	${REGRESS_TARGETS}
.endif
.endif
.endif

.include <bsd.regress.mk>
