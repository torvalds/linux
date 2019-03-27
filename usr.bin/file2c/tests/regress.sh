# $FreeBSD$

echo 1..9

REGRESSION_START($1)

REGRESSION_TEST(`1', `head -c 13 ${SRCDIR}/regress.in | file2c')
REGRESSION_TEST(`2', `head -c 26 ${SRCDIR}/regress.in | file2c PREFIX')
REGRESSION_TEST(`3', `head -c 39 ${SRCDIR}/regress.in | file2c PREFIX SUFFIX')
REGRESSION_TEST(`4', `head -c 52 ${SRCDIR}/regress.in | file2c -x')
REGRESSION_TEST(`5', `head -c 65 ${SRCDIR}/regress.in | file2c -n -1')

REGRESSION_TEST(`6', `head -c  7 ${SRCDIR}/regress.in | file2c -n 1 P S')
REGRESSION_TEST(`7', `head -c 14 ${SRCDIR}/regress.in | file2c -n 2 -x "P S"')
REGRESSION_TEST(`8', `head -c 21 ${SRCDIR}/regress.in | file2c -n 16 P -x S')

REGRESSION_TEST(`9', `file2c "const char data[] = {" ", 0};" <${SRCDIR}/regress.in')

REGRESSION_END()
