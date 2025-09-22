#	$OpenBSD: Makefile,v 1.4 2024/07/02 16:18:11 deraadt Exp $

PROG=		radiusd_radius
BINDIR=		/usr/libexec/radiusd
SRCS=		radiusd_radius.c radiusd_module.c util.c imsg_subr.c log.c
CFLAGS+=	-DUSE_LIBEVENT
LDADD+=		-lradius -lcrypto -lutil -levent
DPADD+=		${LIBRADIUS} ${LIBCRYPTO} ${LIBUTIL} ${LIBEVENT}
MAN=		radiusd_radius.8

.include <bsd.prog.mk>
