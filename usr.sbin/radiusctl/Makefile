#	$OpenBSD: Makefile,v 1.4 2024/07/09 17:26:14 yasuoka Exp $
PROG=		radiusctl
SRCS=		radiusctl.c parser.c chap_ms.c json.c
MAN=		radiusctl.8
CFLAGS+=	-Wall -Wextra -Wno-unused-parameter
CFLAGS+=	-I${.CURDIR}/../radiusd
LDADD+=		-lradius -lcrypto -levent -lutil
DPADD+=		${LIBRADIUS} ${LIBCRYPTO} ${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
