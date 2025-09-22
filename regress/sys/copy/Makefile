#	$OpenBSD: Makefile,v 1.7 2016/09/01 10:12:35 tedu Exp $

PROG=copy
CFLAGS+= -Wall

# Distinct address space:	hppa m88k powerpc sparc64
# Shared address space:		alpha amd64 arm i386 mips

.if (${MACHINE_ARCH} == "hppa" || ${MACHINE_ARCH} == "m88k" || \
    ${MACHINE_ARCH} == "powerpc" || ${MACHINE_ARCH} == "sparc64")
REGRESS_SKIP=
.endif

.include <bsd.regress.mk>
