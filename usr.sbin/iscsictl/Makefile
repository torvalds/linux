#	$OpenBSD: Makefile,v 1.6 2014/04/21 20:20:37 claudio Exp $

.PATH:		${.CURDIR}/../iscsid

PROG=	iscsictl
SRCS=	iscsictl.c parse.y parser.c
SRCS+=	util.c

MAN=	iscsictl.8 iscsi.conf.5

CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../iscsid
YFLAGS=
LDADD= -lutil
DPADD+= ${LIBUTIL}

.include <bsd.prog.mk>
