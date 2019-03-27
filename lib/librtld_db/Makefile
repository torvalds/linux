# $FreeBSD$

.include <bsd.own.mk>

PACKAGE=lib${LIB}
LIB=		rtld_db
SHLIB_MAJOR= 	2
MAN=		librtld_db.3

SRCS=		rtld_db.c
INCS=		rtld_db.h

CFLAGS+= -I${.CURDIR}
# Avoid circular dependency, we only need the libproc.h header here.
CFLAGS+= -I${.CURDIR:H}/libproc

LIBADD+=	elf procstat

.include <bsd.lib.mk>
