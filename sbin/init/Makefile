#	@(#)Makefile	8.1 (Berkeley) 7/19/93
# $FreeBSD$

CONFGROUPS=	CONFTTYS
PACKAGE=runtime
PROG=	init
SRCS=	init.c getmntopts.c
MAN=	init.8
PRECIOUSPROG=
INSTALLFLAGS=-b -B.bak
CFLAGS+=-DDEBUGSHELL -DSECURE -DLOGIN_CAP -DCOMPAT_SYSV_INIT
LIBADD=	util crypt

CONFTTYSNAME=	ttys
.if exists(${.CURDIR}/ttys.${MACHINE})
CONFTTYS+=	ttys.${MACHINE}
.elif exists(${.CURDIR}/ttys.${MACHINE_ARCH})
CONFTTYS+=	ttys.${MACHINE_ARCH}
.elif exists(${.CURDIR}/ttys.${MACHINE_CPUARCH})
CONFTTYS+=	ttys.${MACHINE_CPUARCH}
.else
.error MACHINE.ttys missing
.endif

# Needed for getmntopts.c
MOUNT=	${SRCTOP}/sbin/mount
CFLAGS+=-I${MOUNT}
.PATH:	${MOUNT}

NO_SHARED?=	YES

.include <bsd.prog.mk>
