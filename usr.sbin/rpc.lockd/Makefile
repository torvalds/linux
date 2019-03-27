#	$NetBSD: Makefile,v 1.12 2000/08/07 16:23:31 thorpej Exp $
# $FreeBSD$

PROG=	rpc.lockd
MAN=	rpc.lockd.8
MLINKS=	rpc.lockd.8 lockd.8
SRCS=	kern.c nlm_prot_svc.c lockd.c lock_proc.c lockd_lock.c

CFLAGS+= -I. -I${SYSROOT:U${DESTDIR}}/usr/include/rpcsvc
WARNS?= 3

LIBADD=	rpcsvc

CLEANFILES= nlm_prot_svc.c nlm_prot.h test

RPCSRC= ${SYSROOT:U${DESTDIR}}/usr/include/rpcsvc/nlm_prot.x
RPCGEN= RPCGEN_CPP=${CPP:Q} rpcgen -L -C

nlm_prot_svc.c: ${RPCSRC}
	${RPCGEN} -m -o ${.TARGET} ${.ALLSRC}

nlm_prot.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${.ALLSRC}

test: ${.CURDIR}/test.c
	cc -o test ${.CURDIR}/test.c -lrpcsvc

.include <bsd.prog.mk>
