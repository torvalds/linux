#	$OpenBSD: Makefile,v 1.7 2007/12/09 19:48:55 martin Exp $

.if (${MACHINE} == "alpha" || ${MACHINE} == "amd64" || ${MACHINE} == "i386")
PROG=	mount_ntfs
SRCS=	mount_ntfs.c getmntopts.c
.else
NOPROG=
.endif

MAN=	mount_ntfs.8
MOUNT=	${.CURDIR}/../mount
CFLAGS+= -I${MOUNT}
.PATH:	${MOUNT}

.include <bsd.prog.mk>
