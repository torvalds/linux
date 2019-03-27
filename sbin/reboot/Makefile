#	@(#)Makefile	8.1 (Berkeley) 6/5/93
# $FreeBSD$

PACKAGE=runtime
PROG=	reboot
MAN=	reboot.8 nextboot.8
MLINKS=	reboot.8 halt.8 reboot.8 fastboot.8 reboot.8 fasthalt.8

.if exists(${.CURDIR}/boot_${MACHINE}.8)
MAN+=	boot_${MACHINE}.8
MLINKS+= boot_${MACHINE}.8 boot.8
.endif
.if ${MACHINE} == "amd64"
MAN+=	boot_i386.8
MLINKS+= boot_i386.8 boot.8
.endif

LINKS=	${BINDIR}/reboot ${BINDIR}/halt ${BINDIR}/reboot ${BINDIR}/fastboot \
	${BINDIR}/reboot ${BINDIR}/fasthalt

SCRIPTS=	nextboot.sh

.include <bsd.prog.mk>
