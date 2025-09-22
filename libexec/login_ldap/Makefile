# $Id: Makefile,v 1.1 2020/09/12 15:06:12 martijn Exp $

PROG=		login_ldap
SRCS=		aldap.c util.c search.c bind.c
SRCS+=		login_ldap.c
MAN=		login_ldap.8

CFLAGS+=	-Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare

LDADD+=		-levent -ltls -lssl -lcrypto -lutil
DPADD+=		${LIBEVENT} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO} ${LIBUTIL}
BINDIR=		/usr/libexec/auth

.include <bsd.prog.mk>
