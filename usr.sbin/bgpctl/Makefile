#	$OpenBSD: Makefile,v 1.20 2025/02/20 19:48:14 claudio Exp $

.PATH:		${.CURDIR}/../bgpd

PROG=	bgpctl
SRCS=	bgpctl.c output.c output_json.c output_ometric.c parser.c \
	mrtparser.c json.c ometric.c
SRCS+=	util.c flowspec.c monotime.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../bgpd
MAN=	bgpctl.8
LDADD= -lutil -lm
DPADD+= ${LIBUTIL} ${LIBM}

.include <bsd.prog.mk>
