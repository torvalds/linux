#	$OpenBSD: Makefile,v 1.5 2019/12/01 04:49:29 deraadt Exp $

NOMAN=

.if ${MACHINE} == "octeon"
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
