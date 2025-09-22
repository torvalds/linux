#	$OpenBSD: Makefile,v 1.1 2024/07/09 17:26:14 yasuoka Exp $

PROG=		radiusd_ipcp
BINDIR=		/usr/libexec/radiusd
SRCS=		radiusd_ipcp.c radiusd_module.c log.c
CFLAGS+=	-DUSE_LIBEVENT
LDADD+=		-lradius -lcrypto -lutil -levent
DPADD+=		${LIBRADIUS} ${LIBCRYPTO} ${LIBUTIL} ${LIBEVENT}
MAN=		radiusd_ipcp.8

.include <bsd.prog.mk>
