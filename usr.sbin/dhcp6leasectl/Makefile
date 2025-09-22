#	$OpenBSD: Makefile,v 1.1 2024/06/06 15:16:57 florian Exp $

PROG=	dhcp6leasectl
SRCS=	dhcp6leasectl.c

MAN=	dhcp6leasectl.8

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../../sbin/dhcp6leased
LDADD=	-lutil
DPADD=	${LIBUTIL}

.include <bsd.prog.mk>
