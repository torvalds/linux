#!/bin/sh
# Regression test for GNU grep.

: ${srcdir=.}
: ${GREP=../src/grep}

failures=0

# The Khadafy test is brought to you by Scott Anderson . . .

${GREP} -E -f $srcdir/khadafy.regexp $srcdir/khadafy.lines > khadafy.out
if cmp $srcdir/khadafy.lines khadafy.out
then
	:
else
	echo Khadafy test failed -- output left on khadafy.out
	failures=1
fi

exit $failures
