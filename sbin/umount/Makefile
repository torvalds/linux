#	@(#)Makefile	8.4 (Berkeley) 6/22/95
#
# $FreeBSD$

PACKAGE=runtime
PROG=	umount
SRCS=	umount.c vfslist.c mounttab.c
MAN=	umount.8

MOUNT=	${.CURDIR:H}/mount
UMNTALL= ${SRCTOP}/usr.sbin/rpc.umntall
CFLAGS+= -I${MOUNT} -I${UMNTALL}

.PATH: ${MOUNT} ${UMNTALL}

.include <bsd.prog.mk>
