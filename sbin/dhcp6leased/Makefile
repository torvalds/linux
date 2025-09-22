#	$OpenBSD: Makefile,v 1.2 2024/06/05 16:15:47 florian Exp $

PROG=	dhcp6leased
SRCS=	control.c dhcp6leased.c engine.c frontend.c log.c
SRCS+=	parse.y printconf.c parse_lease.y

MAN=	dhcp6leased.8 dhcp6leased.conf.5

#DEBUG=	-g -DDEBUG=3 -O0

CFLAGS+= -Wall -I${.CURDIR}
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
YFLAGS=
LDADD+=	-levent -lutil
DPADD+= ${LIBEVENT} ${LIBUTIL}

parse_lease.c:	parse_lease.y
	${YACC.y} -ppl -o ${.TARGET} ${.IMPSRC}

.include <bsd.prog.mk>

# Don't compile dhcp6leased as static binary by default
LDSTATIC=
