#!/bin/sh
# Regression test for GNU grep.

: ${srcdir=.}

failures=0

# . . . and the following by Henry Spencer.

${AWK-awk} -f $srcdir/spencer1.awk $srcdir/spencer1.tests > spencer1.script

sh spencer1.script && exit $failures
exit 1
