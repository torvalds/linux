PROG=		lldpd
SRCS=		lldpd.c pdu.c
SRCS+=		log.c
MAN=		lldpd.8

CFLAGS+=	-Wall -Werror
DEBUG=		-g

LDADD+=		-levent
DPADD+=		${LIBEVENT}

.include <bsd.prog.mk>
