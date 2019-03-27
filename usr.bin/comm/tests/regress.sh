# $FreeBSD$

echo 1..3

REGRESSION_START($1)

REGRESSION_TEST(`00', `comm -12 ${SRCDIR}/regress.00a.in ${SRCDIR}/regress.00b.in')
REGRESSION_TEST(`01', `comm -12 ${SRCDIR}/regress.01a.in ${SRCDIR}/regress.01b.in')
REGRESSION_TEST(`02', `comm ${SRCDIR}/regress.02a.in ${SRCDIR}/regress.02b.in')

REGRESSION_END()
