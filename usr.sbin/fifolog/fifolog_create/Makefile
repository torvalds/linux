# $FreeBSD$

PROG=	fifolog_create

CFLAGS+= -I${.CURDIR:H}/lib

LIBADD=	util fifolog

MAN=	fifolog.1
MLINKS=	fifolog.1 fifolog_create.1 \
	fifolog.1 fifolog_reader.1 \
	fifolog.1 fifolog_writer.1

regress:
	rm -f /tmp/fifolog.?
	./${PROG} /tmp/fifolog.0
	./${PROG} -s 10m /tmp/fifolog.1
	./${PROG} -l 1k /tmp/fifolog.2
	./${PROG} -r 1k /tmp/fifolog.3

.include <bsd.prog.mk>
