# $FreeBSD$

PACKAGE=clibs
LIB=dl
SHLIB_MAJOR=1

.PATH:	${SRCTOP}/lib/libc/gen
CFLAGS+=-I${SRCTOP}/lib/libc/include
CFLAGS+=-DIN_LIBDL
LDFLAGS+=-Wl,-F,libc.so.7
VERSION_DEF=${SRCTOP}/lib/libc/Versions.def
SYMBOL_MAPS=${.CURDIR}/Symbol.map

SRCS =	dlfcn.c

.include <bsd.lib.mk>
