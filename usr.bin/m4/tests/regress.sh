# $FreeBSD$

echo 1..21

test_m4() {
	m4 "${@}" 2>&1 | sed -e "s,${SRCDIR}/,,g"
}

REGRESSION_START($1)

REGRESSION_TEST(`args', `test_m4 ${SRCDIR}/args.m4')
REGRESSION_TEST(`args2', `test_m4 ${SRCDIR}/args2.m4')
REGRESSION_TEST(`comments', `test_m4 ${SRCDIR}/comments.m4')
REGRESSION_TEST(`esyscmd', `test_m4 ${SRCDIR}/esyscmd.m4')
REGRESSION_TEST(`eval', `test_m4 ${SRCDIR}/eval.m4')
REGRESSION_TEST(`ff_after_dnl', `uudecode -o /dev/stdout ${SRCDIR}/ff_after_dnl.m4.uu | m4')
REGRESSION_TEST(`gnueval', `test_m4 -g ${SRCDIR}/gnueval.m4')
REGRESSION_TEST(`gnuformat', `test_m4 -g ${SRCDIR}/gnuformat.m4')
REGRESSION_TEST(`gnupatterns', `test_m4 -g ${SRCDIR}/gnupatterns.m4')
REGRESSION_TEST(`gnupatterns2', `test_m4 -g ${SRCDIR}/gnupatterns2.m4')
REGRESSION_TEST(`gnuprefix', `test_m4 -P ${SRCDIR}/gnuprefix.m4 2>&1')
REGRESSION_TEST(`gnusofterror', `test_m4 -g ${SRCDIR}/gnusofterror.m4 2>&1')
REGRESSION_TEST(`gnutranslit2', `test_m4 -g ${SRCDIR}/translit2.m4')
REGRESSION_TEST(`includes', `test_m4 -I${SRCDIR} ${SRCDIR}/includes.m4')
REGRESSION_TEST(`m4wrap3', `test_m4 ${SRCDIR}/m4wrap3.m4')
REGRESSION_TEST(`patterns', `test_m4 ${SRCDIR}/patterns.m4')
REGRESSION_TEST(`quotes', `test_m4 ${SRCDIR}/quotes.m4 2>&1')
REGRESSION_TEST(`strangequotes', `uudecode -o /dev/stdout ${SRCDIR}/strangequotes.m4.uu | m4')
REGRESSION_TEST(`redef', `test_m4 ${SRCDIR}/redef.m4')
REGRESSION_TEST(`translit', `test_m4 ${SRCDIR}/translit.m4')
REGRESSION_TEST(`translit2', `test_m4 ${SRCDIR}/translit2.m4')

REGRESSION_END()
