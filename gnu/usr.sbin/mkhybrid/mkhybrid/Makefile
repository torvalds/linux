#	$OpenBSD: Makefile,v 1.9 2025/08/15 14:49:43 deraadt Exp $

PROG=	mkhybrid
MAN=	mkhybrid.8
BINDIR=	/usr/sbin
.PATH:	${.CURDIR}/../src ${.CURDIR}/../src/libhfs_iso ${.CURDIR}/../src/libfile

SRCS=	data.c block.c low.c lfile.c btree.c node.c record.c lvolume.c \
	hfs.c file.c apprentice.c softmagic.c mkisofs.c tree.c write.c \
	hash.c rock.c multi.c joliet.c match.c name.c eltorito.c \
	apple.c volume.c desktop.c mac_label.c		

CFLAGS+=-DSYSTEM_ID_DEFAULT=\"OpenBSD\" -DAPPLE_HYB -DVANILLA_AUTOCONF \
	-I${.CURDIR}/../src -I${.CURDIR}/../src/include \
	-I${.CURDIR}/../src/libhfs_iso \
	-I${.CURDIR}/../src/libfile

.include <bsd.prog.mk>
