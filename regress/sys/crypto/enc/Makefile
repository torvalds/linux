#       $OpenBSD: Makefile,v 1.10 2017/06/22 18:07:31 bluhm Exp $

DIR=	${.CURDIR}/../../../../sys

CFLAGS+=	-I${DIR}

PROG=   des3
SRCS=	des3.c
LDADD=-lcrypto
DPADD=${LIBCRYPTO}

.PATH:	${DIR}/crypto
SRCS+=	aes.c cast.c ecb_enc.c ecb3_enc.c gmac.c rijndael.c set_key.c
SRCS+=	chachapoly.c poly1305.c
SRCS+=	xform.c

.include <bsd.regress.mk>
