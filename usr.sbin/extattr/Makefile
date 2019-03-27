# $FreeBSD$

PROG=	rmextattr
MAN=	rmextattr.8

LIBADD=	sbuf

LINKS+=	${BINDIR}/rmextattr ${BINDIR}/getextattr
LINKS+=	${BINDIR}/rmextattr ${BINDIR}/setextattr
LINKS+=	${BINDIR}/rmextattr ${BINDIR}/lsextattr

MLINKS+=	rmextattr.8 setextattr.8
MLINKS+=	rmextattr.8 getextattr.8
MLINKS+=	rmextattr.8 lsextattr.8

.include <src.opts.mk>

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
