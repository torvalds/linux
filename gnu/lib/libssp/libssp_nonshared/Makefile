# $FreeBSD$

GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs

.PATH: ${GCCLIB}/libssp ${GCCLIB}/libssp/ssp

LIB=	ssp_nonshared
NO_PIC=
MK_PROFILE=no

SRCS=	ssp-local.c

CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -I${.CURDIR}/..  -I${GCCLIB}/libssp  -I${GCCLIB}/include
CFLAGS+= -fPIC -DPIC -fvisibility=hidden

.include <bsd.lib.mk>
