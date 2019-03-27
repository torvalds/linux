#!/bin/sh
# Regression test for GNU grep.

: ${srcdir=.}

failures=0

# . . . and the following by Henry Spencer.

${AWK-awk} -f $srcdir/ere.awk $srcdir/ere.tests > ere.script

sh ere.script && exit $failures
exit 1
