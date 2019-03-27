#	@(#)Makefile	8.2 (Berkeley) 3/27/94
#
# $FreeBSD$

PACKAGE=runtime
PROG=	mount_nfs
SRCS=	mount_nfs.c getmntopts.c mounttab.c
MAN=	mount_nfs.8

MOUNT=	${.CURDIR:H}/mount
UMNTALL= ${SRCTOP}/usr.sbin/rpc.umntall
CFLAGS+= -DNFS -I${MOUNT} -I${UMNTALL}

.PATH: ${MOUNT} ${UMNTALL}

.include <bsd.prog.mk>
