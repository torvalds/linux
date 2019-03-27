#!/bin/sh
# Regression test for GNU grep.

: ${srcdir=.}

failures=0

# . . . and the following by Henry Spencer.

${AWK-awk} -f $srcdir/scriptgen.awk $srcdir/spencer2.tests > tmp2.script

sh tmp2.script && exit $failures
exit 1
