#	@(#)Makefile	8.1 (Berkeley) 6/4/93
# $FreeBSD$

PROG=	ntalkd
SRCS=	talkd.c announce.c process.c table.c print.c ttymsg.c
.PATH:  ${SRCTOP}/usr.bin/wall
MAN=	talkd.8
CFLAGS+=-I${SRCTOP}/usr.bin/wall

.include <bsd.prog.mk>
