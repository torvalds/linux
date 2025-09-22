#	$OpenBSD: Makefile,v 1.2 2024/07/17 11:20:24 deraadt Exp $

PROG=		radiusd_eap2mschap
BINDIR=		/usr/libexec/radiusd
SRCS=		radiusd_eap2mschap.c radiusd_module.c radius_subr.c log.c
CFLAGS+=	-DUSE_LIBEVENT
LDADD+=		-lradius -lutil -lcrypto -levent
DPADD+=		${LIBRADIUS} ${LIBUTIL} ${LIBCRYPTO} ${LIBEVENT}
MAN=		radiusd_eap2mschap.8

.include <bsd.prog.mk>
