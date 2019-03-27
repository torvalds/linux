# $FreeBSD$

SHLIBDIR?=	/lib

MK_PROFILE=	no
MK_SSP=		no

.include <bsd.own.mk>

GCCDIR=	${SRCTOP}/contrib/gcc
GCCLIB=	${SRCTOP}/contrib/gcclibs
SRCDIR=	${GCCLIB}/libssp

.PATH: ${SRCDIR} ${SRCDIR}/ssp

LIB=		ssp
SHLIB_MAJOR=	0
LD_FATAL_WARNINGS=	no

SRCS=	ssp.c gets-chk.c memcpy-chk.c memmove-chk.c mempcpy-chk.c \
	memset-chk.c snprintf-chk.c sprintf-chk.c stpcpy-chk.c \
	strcat-chk.c strcpy-chk.c strncat-chk.c strncpy-chk.c \
	vsnprintf-chk.c vsprintf-chk.c

CFLAGS+= -DHAVE_CONFIG_H
CFLAGS+= -I${.CURDIR}  -I${SRCDIR}  -I${GCCLIB}/include

VERSION_MAP= ${SRCDIR}/ssp.map


INCS=	ssp.h string.h stdio.h unistd.h
INCSDIR=${INCLUDEDIR}/ssp

CLEANFILES= ssp.h
ssp.h: ssp.h.in
	sed -e 's/@ssp_have_usable_vsnprintf@/define/' ${.ALLSRC} > ${.TARGET}

SUBDIR+= libssp_nonshared

.include <bsd.lib.mk>
