# $FreeBSD$

.include <src.opts.mk>

ELFTCDIR=	${SRCTOP}/contrib/elftoolchain
SRCDIR=		${ELFTCDIR}/cxxfilt

.PATH: ${SRCDIR}

PROG=	c++filt
SRCS=	cxxfilt.c

LIBADD=	elftc

CFLAGS+=-I${ELFTCDIR}/libelftc -I${ELFTCDIR}/common

.include <bsd.prog.mk>
