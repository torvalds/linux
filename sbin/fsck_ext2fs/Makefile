#	$OpenBSD: Makefile,v 1.9 2024/12/18 10:36:05 sthen Exp $

PROG=	fsck_ext2fs
MAN=	fsck_ext2fs.8
SRCS=	dir.c inode.c main.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c fsutil.c setup.c utilities.c ext2fs_bswap.c
.PATH:	${.CURDIR}/../../sys/ufs/ext2fs ${.CURDIR}/../fsck
CFLAGS+= -I${.CURDIR}/../fsck
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.include <bsd.prog.mk>
