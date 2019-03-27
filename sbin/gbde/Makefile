# $FreeBSD$

PACKAGE=runtime
PROG=	gbde
SRCS=	gbde.c template.c
SRCS+=	rijndael-alg-fst.c
SRCS+=	rijndael-api-fst.c
SRCS+=	sha512c.c
SRCS+=	g_bde_lock.c

# rijndael-fst.c does evil casting things which can results in warnings,
# the test-vectors check out however, so it works right.
NO_WCAST_ALIGN=
NO_WMISSING_VARIABLE_DECLARATIONS=

CFLAGS+= -I${SRCTOP}/sys
.PATH:	${SRCTOP}/sys/geom/bde \
	${SRCTOP}/sys/crypto/rijndael \
	${SRCTOP}/sys/crypto/sha2

CLEANFILES+= template.c

MAN=	gbde.8
LIBADD=	md util geom

template.c: template.txt
	file2c 'const char template[] = {' ',0};' \
		< ${.CURDIR}/template.txt > template.c

test: ${PROG}
	sh ${.CURDIR}/test.sh ${.CURDIR}

.include <bsd.prog.mk>
