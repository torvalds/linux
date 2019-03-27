# $FreeBSD$

PACKAGE=lib${LIB}
LIB=	ufs
SHLIBDIR?= /lib
SHLIB_MAJOR=	7

SRCS=	block.c cgroup.c crc32.c inode.c sblock.c type.c ffs_subr.c ffs_tables.c
INCS=	libufs.h

MAN=	bread.3 cgread.3 getinode.3 libufs.3 sbread.3 ufs_disk_close.3
MLINKS+= bread.3 bwrite.3
MLINKS+= bread.3 berase.3
MLINKS+= cgread.3 cgread1.3
MLINKS+= cgread.3 cgget.3
MLINKS+= cgread.3 cgwrite.3
MLINKS+= cgread.3 cgwrite1.3
MLINKS+= cgread.3 cgput.3
MLINKS+= getinode.3 putinode.3
MLINKS+= sbread.3 sbwrite.3
MLINKS+= sbread.3 sbget.3
MLINKS+= sbread.3 sbput.3
MLINKS+= ufs_disk_close.3 ufs_disk_fillout.3
MLINKS+= ufs_disk_close.3 ufs_disk_fillout_blank.3
MLINKS+= ufs_disk_close.3 ufs_disk_write.3

.PATH:  ${SRCTOP}/sys/libkern ${SRCTOP}/sys/ufs/ffs

WARNS?=	2

CFLAGS+= -D_LIBUFS
.if defined(LIBUFS_DEBUG)
CFLAGS+= -D_LIBUFS_DEBUGGING
.endif
CFLAGS+= -I${.CURDIR}

.include <bsd.lib.mk>
