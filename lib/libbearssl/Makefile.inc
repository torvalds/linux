# $FreeBSD$

BEARSSL?= ${SRCTOP}/contrib/bearssl
BEARSSL_SRC= ${BEARSSL}/src

CFLAGS+= -I${BEARSSL}/inc

