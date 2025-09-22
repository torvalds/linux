#	$OpenBSD: Makefile,v 1.10 2010/07/20 02:05:15 deraadt Exp $

PROG=	ifstated
SRCS=	ifstated.c log.c parse.y
CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
YFLAGS=
MAN= ifstated.8 ifstated.conf.5
LDADD+=-levent
DPADD+= ${LIBEVENT}

.include <bsd.prog.mk>
