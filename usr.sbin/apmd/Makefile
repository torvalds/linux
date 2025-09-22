#	$OpenBSD: Makefile,v 1.16 2021/03/29 20:10:30 kn Exp $

.if ((${MACHINE} == "arm64") || (${MACHINE} == "amd64") || \
    (${MACHINE} == "i386") || (${MACHINE} == "loongson") || \
    (${MACHINE} == "macppc") || (${MACHINE} == "sparc64")) || \
    (${MACHINE} == "powerpc64")

SRCS=	apmd.c apmsubr.c

PROG=	apmd
.else
NOPROG=yes
.endif

MAN=	apmd.8

.include <bsd.prog.mk>
