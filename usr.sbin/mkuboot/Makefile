#	$OpenBSD: Makefile,v 1.5 2017/10/27 08:33:40 kevlo Exp $

.if ${MACHINE} == "arm64" || ${MACHINE} == "armv7"

PROG=	mkuboot
SRCS=	mkuboot.c copy_elf32.c copy_elf64.c
DPADD=	${LIBZ}
LDADD=	-lz
CFLAGS=	-DMACHINE_ARCH=\"${MACHINE_ARCH}\"

.else

NOPROG=yes

.endif

MAN=	mkuboot.8

.include <bsd.prog.mk>
