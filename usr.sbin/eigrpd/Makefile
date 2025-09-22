#	$OpenBSD: Makefile,v 1.2 2016/09/02 17:59:58 benno Exp $

PROG=	eigrpd
SRCS=	control.c eigrpd.c eigrpe.c hello.c in_cksum.c interface.c \
	kroute.c log.c logmsg.c neighbor.c packet.c parse.y printconf.c \
	query.c rde.c rde_dual.c reply.c rtp.c tlv.c update.c util.c

MAN=	eigrpd.8 eigrpd.conf.5

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
YFLAGS=
LDADD+=	-levent -lutil
DPADD+= ${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
