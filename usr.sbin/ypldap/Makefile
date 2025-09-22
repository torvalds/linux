# $OpenBSD: Makefile,v 1.11 2019/05/11 17:46:02 rob Exp $

PROG=		ypldap
SRCS=		parse.y ypldap.c log.c	\
		ldapclient.c entries.c yp.c \
		aldap.c \
		ypldap_dns.c

MAN=		ypldap.8 ypldap.conf.5

DPADD=		${LIBTLS} ${LIBSSL} ${LIBCRYPTO} ${LIBEVENT} ${LIBUTIL} \
		${LIBRPCSVC}
LDADD=		-ltls -lssl -lcrypto -levent -lutil -lrpcsvc
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-Wall
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
YFLAGS=

.include <bsd.prog.mk>
