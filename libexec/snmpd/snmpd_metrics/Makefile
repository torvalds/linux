#	$OpenBSD: Makefile,v 1.1.1.1 2022/09/01 14:20:34 martijn Exp $

PROG=		snmpd_metrics
SRCS=		mib.c log.c kroute.c pf.c timer.c util.c
MAN=		snmpd_metrics.8

CFLAGS+=	-Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare

LDADD=		-lagentx -levent -lkvm
DPADD=		${LIBAGENTX} ${LIBEVENT} ${LIBKVM}
BINDIR=		/usr/libexec/snmpd/

.include <bsd.prog.mk>
