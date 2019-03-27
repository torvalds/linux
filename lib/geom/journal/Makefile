# $FreeBSD$

PACKAGE=runtime
.PATH: ${.CURDIR:H:H}/misc

GEOM_CLASS=	journal
SRCS+=	geom_journal_ufs.c

LIBADD=	ufs md

CFLAGS+=-I${SRCTOP}/sys

.include <bsd.lib.mk>
