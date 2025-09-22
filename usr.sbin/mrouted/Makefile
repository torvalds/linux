#	$OpenBSD: Makefile,v 1.8 2024/02/08 20:28:54 miod Exp $

PROG=	mrouted
SRCS=	callout.c cfparse.c config.c igmp.c inet.c kern.c main.c prune.c \
	route.c vif.c
MAN=	mrouted.8

CFLAGS+=-I${.CURDIR}

CLEANFILES+= cfparse.c cfparse.h y.tab.h

.include <bsd.prog.mk>
