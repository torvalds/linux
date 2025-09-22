#	$OpenBSD: Makefile,v 1.6 2017/06/22 18:07:31 bluhm Exp $

DIR=${.CURDIR}/../../../../sys

PROG=	key_wrap_test
SRCS+=	aes.c key_wrap.c key_wrap_test.c timingsafe_bcmp.c
CDIAGFLAGS=	-Wall
CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wno-uninitialized
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
#CDIAGFLAGS+=	-Wshadow

CFLAGS+=	-I${DIR}

.PATH:	${DIR}/crypto/ ${DIR}/lib/libkern/

run-regress-${PROG}: ${PROG}
	./${PROG} | diff - ${.CURDIR}/${PROG}.txt

.include <bsd.regress.mk>
