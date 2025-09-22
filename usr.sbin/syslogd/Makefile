#	$OpenBSD: Makefile,v 1.9 2022/01/13 10:34:07 martijn Exp $

PROG=	syslogd
SRCS=	evbuffer_tls.c log.c parsemsg.c privsep.c privsep_fdpass.c ringbuf.c \
	syslogd.c ttymsg.c
MAN=	syslogd.8 syslog.conf.5
LDADD=	-levent -ltls -lssl -lcrypto
DPADD=	${LIBEVENT} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

.include <bsd.prog.mk>
