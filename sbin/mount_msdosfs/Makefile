#
# $FreeBSD$
#

PACKAGE=runtime
PROG=	mount_msdosfs
SRCS=	mount_msdosfs.c getmntopts.c
MAN=	mount_msdosfs.8
LIBADD=	kiconv

MOUNT=	${.CURDIR:H}/mount
CFLAGS+= -I${MOUNT}

# Needs to be dynamically linked for optional dlopen() access to
# userland libiconv
NO_SHARED?=	NO

.PATH:	${MOUNT}

.include <bsd.prog.mk>
