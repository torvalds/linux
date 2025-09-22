#	$OpenBSD: Makefile,v 1.6 2021/04/16 14:37:06 claudio Exp $

PROG=	iscsid
SRCS=	connection.c control.c initiator.c iscsid.c log.c logmsg.c pdu.c \
	poll.c session.c task.c util.c vscsi.c

MAN=	iscsid.8

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
LDADD+= -levent
DPADD+= ${LIBEVENT}

.include <bsd.prog.mk>
