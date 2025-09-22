#	$OpenBSD: Makefile,v 1.1 1998/08/15 21:02:22 millert Exp $

PROG=	lockspool
SRCS=	lockspool.c locking.c
BINOWN=	root
BINMODE=4555
CFLAGS+=-I${.CURDIR}/../mail.local
.PATH:	${.CURDIR}/../mail.local

.include <bsd.prog.mk>
