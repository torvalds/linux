#	$OpenBSD: Makefile,v 1.4 2005/08/22 17:26:46 hshoexer Exp $

PROG=	ipsecctl
MAN=	ipsecctl.8 ipsec.conf.5

SRCS=	ike.c ipsecctl.c pfkey.c pfkdump.c parse.y

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare

YFLAGS=

.include <bsd.prog.mk>
