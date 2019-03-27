# $FreeBSD$

.include <src.opts.mk>

PACKAGE=	tests

TESTSDIR=${TESTSBASE}/sys/cddl/zfs/include

STFSUITEDIR=${TESTSBASE}/sys/cddl/zfs

MAN=
FILESDIR=	${TESTSBASE}/sys/cddl/zfs/include
${PACKAGE}FILES+=		constants.cfg
${PACKAGE}FILES+=		libremote.kshlib
${PACKAGE}FILES+=		libgnop.kshlib
${PACKAGE}FILES+=		logapi.kshlib
${PACKAGE}FILES+=		libtest.kshlib
${PACKAGE}FILES+=		stf.shlib
${PACKAGE}FILES+=		testenv.kshlib

${PACKAGE}FILES+=		commands.cfg
CLEANFILES+=	commands.cfg
commands.cfg:	translatecommands.awk commands.txt
	awk -v stfsuitedir=${STFSUITEDIR} -f ${.ALLSRC} > ${.TARGET}

${PACKAGE}FILES+=		default.cfg
CLEANFILES+=	default.cfg
default.cfg:	default.cfg.in
	sed "s:%%STFSUITEDIR%%:${STFSUITEDIR}:" ${.ALLSRC} > ${.TARGET}

ATF_TESTS_KSH93+=	libtest_test
.include <bsd.test.mk>
