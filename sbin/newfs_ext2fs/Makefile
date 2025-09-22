# $OpenBSD: Makefile,v 1.3 2014/07/13 16:44:20 pelikan Exp $
PROG=	newfs_ext2fs
SRCS=	newfs_ext2fs.c mke2fs.c ext2fs_bswap.c
MAN=	newfs_ext2fs.8

.PATH:	${.CURDIR}/../../sys/ufs/ext2fs
DPADD+= ${LIBUTIL}
LDADD+= -lutil

.include <bsd.prog.mk>
