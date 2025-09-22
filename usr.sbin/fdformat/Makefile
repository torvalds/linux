#	$OpenBSD: Makefile,v 1.7 2016/09/01 09:54:58 tedu Exp $

.if ${MACHINE} == "i386" || \
    ${MACHINE} == "sparc64" || ${MACHINE} == "amd64"
PROG=	fdformat
DPADD=	${LIBUTIL}
LDADD=	-lutil
.else
NOPROG=yes
.endif

MAN=fdformat.1

MANSUBDIR=i386 sparc64 amd64

.include <bsd.prog.mk>
