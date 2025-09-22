# $OpenBSD: Makefile,v 1.2 2021/09/06 13:37:50 deraadt Exp $

PROG=		btrace
MAN=		bt.5 btrace.8

SRCS=		bt_parse.y btrace.c ksyms.c map.c printf.c

# Use syscall names generated from sys/kern/makesyscalls.sh
SRCS+=		syscalls.c
CFLAGS+=	-DPTRACE -DKTRACE -DACCOUNTING -DNFSCLIENT -DSYSVSHM -DSYSVSEM
CFLAGS+=	-DSYSVMSG
.PATH:		${.CURDIR}/../../sys/kern

CFLAGS+=	-W -Wall -Wstrict-prototypes -Wno-unused -Wunused-variable
CFLAGS+=	-I${.CURDIR}

LDADD+=		-lelf
DPADD+=		${LIBELF}

#DEBUG?=		-g

.include <bsd.prog.mk>
