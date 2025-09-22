#	$OpenBSD: Makefile,v 1.24 2019/01/17 06:24:15 tedu Exp $

.if ${MACHINE} == "macppc"
PROG=	pdisk
LDADD=  -lutil
DPADD=  ${LIBUTIL}
CFLAGS+=-Wall

SRCS=	dump.c file_media.c io.c partition_map.c pdisk.c

.else
NOPROG=yes
.endif

MAN=	pdisk.8
MANSUBDIR=macppc

.include <bsd.prog.mk>
