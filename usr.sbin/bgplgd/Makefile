#	$OpenBSD: Makefile,v 1.2 2024/01/26 18:11:49 job Exp $

PROG=		bgplgd
SRCS=		bgplgd.c slowcgi.c qs.c
CFLAGS+=	-Wall
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CLFAGS+=	-Wmissing-declarations -Wredundant-decls
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
CFLAGS+=	-I${.CURDIR} -I${.CURDIR}/../bgpd
LDADD=  -levent
DPADD=  ${LIBEVENT}
MAN=		bgplgd.8

.include <bsd.prog.mk>
