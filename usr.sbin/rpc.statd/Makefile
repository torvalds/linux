# $FreeBSD$

PROG=	rpc.statd
MAN=	rpc.statd.8
SRCS=	file.c sm_inter_svc.c sm_inter.h statd.c procs.c

CFLAGS+= -I.
WARNS?= 2

LIBADD=	rpcsvc

CLEANFILES= sm_inter_svc.c sm_inter.h

RPCSRC= ${SYSROOT:U${DESTDIR}}/usr/include/rpcsvc/sm_inter.x
RPCGEN= RPCGEN_CPP=${CPP:Q} rpcgen -L -C

sm_inter_svc.c: ${RPCSRC}
	${RPCGEN} -m -o ${.TARGET} ${.ALLSRC}

sm_inter.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${.ALLSRC}

test: test.c
	cc -o test test.c -lrpcsvc

.include <bsd.prog.mk>
