#	$OpenBSD: Makefile,v 1.26 2024/01/27 09:53:59 martijn Exp $

PROG=		snmpd
MAN=		snmpd.8 snmpd.conf.5
SRCS=		mib.y parse.y log.c snmpe.c application.c application_blocklist.c \
		    application_internal.c application_agentx.c ax.c \
		    trap.c smi.c snmpd.c \
		    proc.c usm.c traphandler.c util.c

LDADD=		-levent -lutil -lcrypto
DPADD=		${LIBEVENT} ${LIBUTIL} ${LIBCRYPTO}
CFLAGS+=	-Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
YFLAGS=

mib.c:		mib.y
	${YACC.y} -pmib -o ${.TARGET} ${.IMPSRC}


.include <bsd.prog.mk>
