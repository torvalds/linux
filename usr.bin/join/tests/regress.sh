# $FreeBSD$

echo 1..1

REGRESSION_START($1)

REGRESSION_TEST_ONE(`join -t , -a1 -a2 -e "(unknown)" -o 0,1.2,2.2 ${SRCDIR}/regress.1.in ${SRCDIR}/regress.2.in')

REGRESSION_END()
