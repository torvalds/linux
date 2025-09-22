# $OpenBSD: Makefile,v 1.7 2018/06/24 10:39:59 kettenis Exp $

.if ${MACHINE} == "amd64" || ${MACHINE} == "arm64" || \
    ${MACHINE} == "i386"
PROG= acpidump
SRCS= acpidump.c
LDADD=	-lkvm
DPADD=	${LIBKVM}
.else
NOPROG= yes
.endif

MAN= acpidump.8

.include <bsd.prog.mk>
