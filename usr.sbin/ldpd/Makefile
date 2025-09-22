#	$OpenBSD: Makefile,v 1.12 2017/01/20 12:19:18 benno Exp $

PROG=	ldpd
SRCS=	accept.c address.c adjacency.c control.c hello.c init.c interface.c \
	keepalive.c kroute.c l2vpn.c labelmapping.c lde.c lde_lib.c ldpd.c \
	ldpe.c log.c logmsg.c neighbor.c notification.c packet.c parse.y \
	pfkey.c printconf.c socket.c util.c

MAN=	ldpd.8 ldpd.conf.5

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
YFLAGS=
LDADD+=	-levent -lutil
DPADD+= ${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
