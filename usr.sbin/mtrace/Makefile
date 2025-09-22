#	$OpenBSD: Makefile,v 1.3 2003/03/04 22:24:35 deraadt Exp $

.PATH: ${.CURDIR}/../mrouted
CFLAGS+= -I${.CURDIR}/../mrouted

PROG=	mtrace
SRCS=	igmp.c inet.c kern.c mtrace.c
MAN=	mtrace.8

.include <bsd.prog.mk>
