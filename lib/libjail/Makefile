# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	jail
SHLIBDIR?= /lib
SHLIB_MAJOR= 1
SRCS=	jail.c jail_getid.c
INCS=	jail.h

PACKAGE=jail

MAN=	jail.3

MLINKS+=jail.3 jail_getid.3
MLINKS+=jail.3 jail_getname.3
MLINKS+=jail.3 jail_getv.3
MLINKS+=jail.3 jail_setv.3
MLINKS+=jail.3 jailparam.3
MLINKS+=jail.3 jailparam_all.3
MLINKS+=jail.3 jailparam_init.3
MLINKS+=jail.3 jailparam_import.3
MLINKS+=jail.3 jailparam_import_raw.3
MLINKS+=jail.3 jailparam_get.3
MLINKS+=jail.3 jailparam_set.3
MLINKS+=jail.3 jailparam_export.3
MLINKS+=jail.3 jailparam_free.3

CFLAGS+=-I${.CURDIR}

.include <bsd.lib.mk>
