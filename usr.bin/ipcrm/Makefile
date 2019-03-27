# $FreeBSD$

PROG=	ipcrm
SRCS=	ipcrm.c ipc.c
LIBADD=	kvm
CFLAGS+=-I${SRCTOP}/usr.bin/ipcs
.PATH:  ${SRCTOP}/usr.bin/ipcs

WARNS?=	0

.include <bsd.prog.mk>
