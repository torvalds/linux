# $FreeBSD$

PROG=	keyserv
MAN=	keyserv.8
SRCS=	keyserv.c setkey.c crypt_svc.c crypt_server.c crypt.h

CFLAGS+= -DKEYSERV_RANDOM -DBROKEN_DES -I.

LIBADD=	mp rpcsvc

WARNS?=	1

RPCDIR=	${SYSROOT:U${DESTDIR}}/usr/include/rpcsvc

CLEANFILES= crypt_svc.c crypt.h

RPCGEN= RPCGEN_CPP=${CPP:Q} rpcgen -C

crypt_svc.c: ${RPCDIR}/crypt.x
	${RPCGEN} -m -o ${.TARGET} ${RPCDIR}/crypt.x

crypt.h: ${RPCDIR}/crypt.x
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/crypt.x

.include <bsd.prog.mk>
