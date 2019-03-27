#	$NetBSD: Makefile,v 1.1.1.1 1995/10/08 23:08:36 thorpej Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD$

PACKAGE=runtime
PROG=	fsdb
MAN=	fsdb.8
SRCS=	fsdb.c fsdbutil.c \
	dir.c ea.c fsutil.c inode.c pass1.c pass1b.c pass2.c pass3.c pass4.c \
	pass5.c setup.c utilities.c ffs_subr.c ffs_tables.c globs.c prtblknos.c
CFLAGS+= -I${.CURDIR:H}/fsck_ffs
WARNS?= 2
LIBADD=	edit ufs
.PATH:	${.CURDIR:H}/fsck_ffs ${SRCTOP}/sys/ufs/ffs \
	${SRCTOP}/tools/diag/prtblknos

.include <bsd.prog.mk>
