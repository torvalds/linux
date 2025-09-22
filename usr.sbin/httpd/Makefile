#	$OpenBSD: Makefile,v 1.31 2024/01/04 18:17:47 espie Exp $

PROG=		httpd
SRCS=		parse.y
SRCS+=		config.c control.c httpd.c log.c logger.c proc.c
SRCS+=		server.c server_http.c server_file.c server_fcgi.c
MAN=		httpd.8 httpd.conf.5

SRCS+=		patterns.c
MAN+=		patterns.7

LDADD=		-levent -ltls -lssl -lcrypto -lutil
DPADD=		${LIBEVENT} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO} ${LIBUTIL}
#DEBUG=		-g -DDEBUG=3 -O0
CFLAGS+=	-Wall -I${.CURDIR} -I${.OBJDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith
CFLAGS+=	-Wsign-compare -Wcast-qual
YFLAGS=

.for h in css.h js.h
$h: $h.in
	sed -f ${.CURDIR}/toheader.sed <${.CURDIR}/$h.in >$@.tmp && mv $@.tmp $@
.endfor

server_file.o: css.h js.h

CLEANFILES += css.h js.h

.include <bsd.prog.mk>
