#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD$

PROG=	fortune
MAN=	fortune.6
CFLAGS+=-DDEBUG -I${FORTUNE_SRC}/strfile

.include <bsd.prog.mk>

test:	${PROG}
	./${PROG} -m brass
