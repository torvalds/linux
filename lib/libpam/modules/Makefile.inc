# $FreeBSD$

PAMDIR=		${SRCTOP}/contrib/openpam

MK_INSTALLLIB=	no
MK_PROFILE=	no

CFLAGS+= -I${PAMDIR}/include -I${SRCTOP}/lib/libpam

SHLIB_NAME?=	${LIB}.so.${SHLIB_MAJOR}
LIBADD+=	pam

.include "../Makefile.inc"
