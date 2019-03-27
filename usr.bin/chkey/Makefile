# $FreeBSD$

.include <src.opts.mk>

.PATH: ${SRCTOP}/usr.bin/newkey

PROG=	chkey
SRCS=	chkey.c generic.c update.c
CFLAGS+= -I${SRCTOP}/usr.bin/newkey
.if ${MK_NIS} != "no"
CFLAGS+= -DYP
.endif
LIBADD+=	rpcsvc mp

.include <bsd.prog.mk>
