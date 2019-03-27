#	@(#)Makefile	8.3 (Berkeley) 3/27/94
# $FreeBSD$

PACKAGE=runtime
PROG=	mount_cd9660
SRCS=	mount_cd9660.c getmntopts.c
MAN=	mount_cd9660.8
LIBADD=	kiconv

MOUNT=	${.CURDIR:H}/mount
CFLAGS+= -I${MOUNT}

# Needs to be dynamically linked for optional dlopen() access to
# userland libiconv
NO_SHARED?=	NO

.PATH:	${MOUNT}

.include <bsd.prog.mk>
