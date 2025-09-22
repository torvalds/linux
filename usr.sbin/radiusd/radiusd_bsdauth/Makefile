#	$OpenBSD: Makefile,v 1.4 2024/07/02 16:18:11 deraadt Exp $

PROG=		radiusd_bsdauth
BINDIR=		/usr/libexec/radiusd
SRCS=		radiusd_bsdauth.c radiusd_module.c imsg_subr.c
LDADD+=		-lradius -lcrypto -lutil
DPADD+=		${LIBRADIUS} ${LIBCRYPTO} ${LIBUTIL}
MAN=		radiusd_bsdauth.8

.include <bsd.prog.mk>
