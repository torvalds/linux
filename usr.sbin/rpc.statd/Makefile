#	$OpenBSD: Makefile,v 1.3 2017/07/10 21:30:37 espie Exp $

PROG=	rpc.statd
SRCS=	procs.c sm_inter_svc.c statd.c
MAN=	rpc.statd.8

DPADD=	${LIBRPCSVC}
LDADD=	-lrpcsvc

CFLAGS+= -I.

CLEANFILES=	sm_inter_svc.c sm_inter.h

RPCSRC=	${DESTDIR}/usr/include/rpcsvc/sm_inter.x
RPCGEN=	rpcgen -L -C

sm_inter_svc.c: ${RPCSRC}
	${RPCGEN} -m -o ${.TARGET} ${RPCSRC}

sm_inter.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${RPCSRC}

BUILDFIRST =	sm_inter.h

test: ${.CURDIR}/test.c
	cc -o test ${.CURDIR}/test.c -lrpcsvc

.include <bsd.prog.mk>
