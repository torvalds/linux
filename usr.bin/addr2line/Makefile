# $FreeBSD$

.include <src.opts.mk>

ELFTCDIR=	${SRCTOP}/contrib/elftoolchain
ADDR2LINEDIR=	${ELFTCDIR}/addr2line

.PATH: ${ADDR2LINEDIR}

PROG=	addr2line

LIBADD=	elftc dwarf elf

CFLAGS+=-I${ELFTCDIR}/libelftc -I${ELFTCDIR}/common

.include <bsd.prog.mk>
