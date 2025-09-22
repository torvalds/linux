#	$OpenBSD: Makefile,v 1.7 2024/07/10 09:27:33 dv Exp $

.if ${MACHINE} == "amd64" || ${MACHINE} == "arm64"

.PATH:	${.CURDIR}/../vmd

PROG=	vmctl
SRCS=	vmctl.c main.c atomicio.c
SRCS+=	vioqcow2.c vioraw.c log.c
CFLAGS+= -Wall
CFLAGS+= -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+= -Wmissing-declarations
CFLAGS+= -Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+= -Wsign-compare
CFLAGS+= -I${.CURDIR} -I${.CURDIR}/../vmd
LDADD+=	-lutil -lpthread
DPADD+= ${LIBUTIL} ${LIBPTHREAD}
PATH+=../vmd

.else

NOPROG= yes

.endif

MAN= vmctl.8

.include <bsd.prog.mk>
