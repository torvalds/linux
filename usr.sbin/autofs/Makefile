# $FreeBSD$

CONFS=	auto_master
PACKAGE=autofs
PROG=	automountd
SRCS=	automount.c
SRCS+=	automountd.c
SRCS+=	autounmountd.c
SRCS+=	common.c
SRCS+=	defined.c
SRCS+=	getmntopts.c
SRCS+=	log.c
SRCS+=	popen.c
SRCS+=	token.l

CFLAGS+=-I${.CURDIR}
CFLAGS+=-I${SRCTOP}/sys/fs/autofs

MAN=	automount.8 automountd.8 autounmountd.8 auto_master.5

LIBADD=	util

# Needed for getmntopts.c
MOUNT=  ${SRCTOP}/sbin/mount
CFLAGS+=-I${MOUNT}

WARNS=	6

LINKS=	${BINDIR}/automountd ${BINDIR}/automount
LINKS+=	${BINDIR}/automountd ${BINDIR}/autounmountd

.PATH:	${MOUNT}

SUBDIR=	autofs

.include <bsd.prog.mk>
