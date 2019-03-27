# $FreeBSD$

.PATH: ${SRCTOP}/sys/netinet/libalias

CONFS=	libalias.conf
PACKAGE=lib${LIB}
LIB=	alias
SHLIBDIR?= /lib
SHLIB_MAJOR= 7
MAN=	libalias.3
SRCS=	alias.c alias_db.c alias_proxy.c alias_util.c alias_mod.c
INCS=	alias.h
NO_WERROR=

.include <bsd.lib.mk>
