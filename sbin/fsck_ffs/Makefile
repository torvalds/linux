#	$OpenBSD: Makefile,v 1.7 2010/06/05 07:36:23 jsing Exp $

PROG=	fsck_ffs
MAN=	fsck_ffs.8
SRCS=	dir.c inode.c main.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c fsutil.c setup.c utilities.c ffs_subr.c ffs_tables.c
.PATH:	${.CURDIR}/../../sys/ufs/ffs ${.CURDIR}/../fsck
CFLAGS+= -I${.CURDIR}/../fsck
DPADD+=	${LIBUTIL}
LDADD+=	-lutil

.include <bsd.prog.mk>
