
OLLDPD_DIR=	${.CURDIR}/../lldpd

.PATH:		${OLLDPD_DIR}

PROG=		lldp
SRCS=		lldp.c pdu.c
MAN=		lldp.8

CFLAGS+=	-I${.CURDIR} -I${OLLDPD_DIR}
CFLAGS+=	-Wall -Werror
DEBUG=		-g

.include <bsd.prog.mk>
