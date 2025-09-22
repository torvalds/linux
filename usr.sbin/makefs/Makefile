#	$OpenBSD: Makefile,v 1.10 2016/10/18 18:42:08 deraadt Exp $
#	$NetBSD: Makefile,v 1.36 2013/08/05 14:41:57 reinoud Exp $
#

.include <bsd.own.mk>

PROG=	makefs

SRCS=	makefs.c walk.c xmalloc.c
SRCS+=	cd9660.c cd9660_strings.c cd9660_debug.c cd9660_eltorito.c
SRCS+=	cd9660_write.c cd9660_conversion.c iso9660_rrip.c
SRCS+=	ffs.c ffs_alloc.c ffs_balloc.c ffs_subr.c
SRCS+=	ffs_tables.c ufs_bmap.c buf.c mkfs.c
SRCS+=	msdos.c mkfs_msdos.c msdosfs_fat.c msdosfs_conv.c msdosfs_vfsops.c
SRCS+=	msdosfs_lookup.c msdosfs_denode.c msdosfs_vnops.c msdosfs_unicode.c

MAN=	makefs.8

CPPFLAGS+=	-I${.CURDIR}
#CPPFLAGS+=	-DMSDOSFS_DEBUG

DPADD+= ${LIBUTIL}
LDADD+= -lutil

.PATH:	${.CURDIR}/cd9660 ${.CURDIR}/ffs ${.CURDIR}/msdos

.include <bsd.prog.mk>
