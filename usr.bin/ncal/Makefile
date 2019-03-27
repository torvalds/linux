# $FreeBSD$

.include <src.opts.mk>

PROG=	ncal

LIBADD=	calendar ncursesw

LINKS=	${BINDIR}/ncal ${BINDIR}/cal
MLINKS=	ncal.1 cal.1

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
