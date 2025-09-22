#	$OpenBSD: Makefile,v 1.5 2001/05/14 07:22:06 angelos Exp $

PROG=	iostat
MAN=	iostat.8

.PATH:	${.CURDIR}/../../usr.bin/vmstat

CFLAGS+=-I${.CURDIR}/../../usr.bin/vmstat

# dkstats.c pulled in from ../../usr.bin/vmstat
SRCS=	dkstats.c iostat.c

DPADD=	${LIBKVM}
LDADD=	-lkvm

.include <bsd.prog.mk>
