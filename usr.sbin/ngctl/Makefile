# $FreeBSD$
# $Whistle: Makefile,v 1.3 1999/01/16 00:10:11 archie Exp $

.include <src.opts.mk>

PROG=	ngctl
MAN=	ngctl.8
SRCS=	main.c mkpeer.c config.c connect.c dot.c name.c show.c list.c \
	msg.c debug.c shutdown.c rmhook.c status.c types.c write.c
WARNS?=	3

.if defined(RELEASE_CRUNCH)
NGCTL_NO_LIBEDIT=
.endif

.if ${MK_LIBTHR} == "no"
NGCTL_NO_LIBEDIT=
.endif

LIBADD=	netgraph

.if !defined(NGCTL_NO_LIBEDIT)
CFLAGS+= -DEDITLINE
LIBADD+=	edit pthread
.endif

.include <bsd.prog.mk>
