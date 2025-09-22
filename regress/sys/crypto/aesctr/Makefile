#       $OpenBSD: Makefile,v 1.6 2017/06/22 18:07:30 bluhm Exp $

DIR=	${.CURDIR}/../../../../sys

CFLAGS+=	-I${DIR}

PROG=   aesctr
SRCS=	aesctr.c

CDIAGFLAGS=	-Wall
CDIAGFLAGS+=	-Werror
CDIAGFLAGS+=	-Wpointer-arith
CDIAGFLAGS+=	-Wno-uninitialized
CDIAGFLAGS+=	-Wstrict-prototypes
CDIAGFLAGS+=	-Wmissing-prototypes
CDIAGFLAGS+=	-Wunused
CDIAGFLAGS+=	-Wsign-compare
CDIAGFLAGS+=	-Wshadow

.PATH:	${DIR}/crypto
SRCS+=	cast.c ecb_enc.c ecb3_enc.c gmac.c aes.c rijndael.c set_key.c
SRCS+=	chachapoly.c poly1305.c
SRCS+=	xform.c

.include <bsd.regress.mk>
