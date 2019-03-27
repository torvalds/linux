# $FreeBSD$

PACKAGE=	tests

.include <bsd.own.mk>

LIBXOSRC=	${SRCTOP}/contrib/libxo

.PATH: ${LIBXOSRC}/tests/xo ${LIBXOSRC}/tests/xo/saved

.c.out .o.out:

BINDIR=		${TESTSDIR}

ATF_TESTS_SH+=	functional_test

${PACKAGE}FILES+=		xo_01.H.err
${PACKAGE}FILES+=		xo_01.H.out
${PACKAGE}FILES+=		xo_01.HIPx.err
${PACKAGE}FILES+=		xo_01.HIPx.out
${PACKAGE}FILES+=		xo_01.HP.err
${PACKAGE}FILES+=		xo_01.HP.out
${PACKAGE}FILES+=		xo_01.J.err
${PACKAGE}FILES+=		xo_01.J.out
${PACKAGE}FILES+=		xo_01.JP.err
${PACKAGE}FILES+=		xo_01.JP.out
${PACKAGE}FILES+=		xo_01.T.err
${PACKAGE}FILES+=		xo_01.T.out
${PACKAGE}FILES+=		xo_01.X.err
${PACKAGE}FILES+=		xo_01.X.out
${PACKAGE}FILES+=		xo_01.XP.err
${PACKAGE}FILES+=		xo_01.XP.out

SCRIPTS+=	xo_01
CLEANFILES+=	xo_01

LIBADD+=	xo

.include <bsd.test.mk>

.SUFFIXES: .sh
