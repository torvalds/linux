#	$OpenBSD: Makefile,v 1.2 2013/06/03 10:37:02 espie Exp $
#	$NetBSD: Makefile,v 1.5 2009/04/11 07:58:12 lukem Exp $

.include <bsd.own.mk>

PROG=		mount_tmpfs
#SRCS=		mount_tmpfs.c fattr.c pathadj.c
SRCS=		mount_tmpfs.c getmntopts.c
MAN=		mount_tmpfs.8

MOUNT=		${.CURDIR}/../mount
.PATH:		${MOUNT}

CPPFLAGS+=	-I${MOUNT}
DPADD+=		${LIBUTIL}
LDADD+=		-lutil

.include <bsd.prog.mk>
