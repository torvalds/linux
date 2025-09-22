#	$OpenBSD: Makefile,v 1.1.1.1 2018/04/27 16:14:35 eric Exp $

PROG=		lpd

SRCS+=		control.c
SRCS+=		engine.c
SRCS+=		engine_lpr.c
SRCS+=		frontend.c
SRCS+=		frontend_lpr.c
SRCS+=		iobuf.c
SRCS+=		io.c
SRCS+=		log.c
SRCS+=		logmsg.c
SRCS+=		lp.c
SRCS+=		lp_banner.c
SRCS+=		lp_displayq.c
SRCS+=		lp_stty.c
SRCS+=		lp_rmjob.c
SRCS+=		lpd.c
SRCS+=		parse.y
SRCS+=		printer.c
SRCS+=		proc.c
SRCS+=		resolver.c

NOMAN=		noman
BINDIR=		/usr/sbin

CFLAGS+= -Wall -I${.CURDIR}
YFLAGS=

LDADD+=		-levent -lutil
DPADD+=		${LIBEVENT} ${LIBUTIL}

.include <bsd.prog.mk>
