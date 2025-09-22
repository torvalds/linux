#	$OpenBSD: Makefile,v 1.1 2020/07/16 19:48:58 kettenis Exp $

NOMAN=

.if ${MACHINE} == "powerpc64"
PROG=		rdboot
SRCS=		cmd.c disk.c rdboot.c vars.c
LDADD+=		-lutil
LDSTATIC+=	-static
.else
NOPROG=
.endif

.PATH:  ${S}/lib/libsa
#SRCS+=  hexdump.c strtoll.c

install:

.include <bsd.prog.mk>
