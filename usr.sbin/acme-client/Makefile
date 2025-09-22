#	$OpenBSD: Makefile,v 1.9 2019/06/12 11:09:25 gilles Exp $
PROG=		acme-client
SRCS=		acctproc.c base64.c certproc.c chngproc.c dbg.c dnsproc.c
SRCS+=		fileproc.c http.c jsmn.c json.c keyproc.c main.c netproc.c
SRCS+=		parse.y revokeproc.c key.c util.c

MAN=		acme-client.1 acme-client.conf.5

LDADD=		-ltls -lssl -lcrypto
DPADD=		${LIBTLS} ${LIBSSL} ${LIBCRYPTO}

CFLAGS+=	-W -Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith
CFLAGS+=	-Wsign-compare -Wunused
YFLAGS=

.include <bsd.prog.mk>
