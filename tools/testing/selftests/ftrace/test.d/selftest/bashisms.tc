#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# description: Meta-selftest: Checkbashisms

if [ ! -f $FTRACETEST_ROOT/ftracetest ]; then
  echo "Hmm, we can not find ftracetest"
  exit_unresolved
fi

if ! which checkbashisms > /dev/null 2>&1 ; then
  echo "No checkbashisms found. skipped."
  exit_unresolved
fi

checkbashisms $FTRACETEST_ROOT/ftracetest
checkbashisms $FTRACETEST_ROOT/test.d/functions
for t in $(find $FTRACETEST_ROOT/test.d -name \*.tc); do
  checkbashisms $t
done

exit 0
