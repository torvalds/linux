#!/bin/sh
# Test that backrefs are local to regex.
#
#

: ${srcdir=.}

failures=0

# checking for a palindrome
echo "radar" | ${GREP} -e '\(.\)\(.\).\2\1' > /dev/null 2>&1
if test $? -ne 0 ; then
        echo "backref: palindrome, test \#1 failed"
        failures=1
fi

# hit hard with the `Bond' tests
echo "civic" | ${GREP} -E -e '^(.?)(.?)(.?)(.?)(.?)(.?)(.?)(.?)(.?).?\9\8\7\6\5\4\3\2\1$' > /dev/null 2>&1
if test $? -ne 0 ; then
        echo "Options: Bond, test \#2 failed"
        failures=1
fi

# backref are local should be error
echo "123" | ${GREP} -e 'a\(.\)' -e 'b\1' > /dev/null 2>&1
if test $? -ne 2 ; then
	echo "Options: Backref not local, test \#3 failed"
	failures=1
fi

# Pattern should faile
echo "123" | ${GREP} -e '[' -e ']' > /dev/null 2>&1
if test $? -ne 2 ; then
	echo "Options: Compiled not local, test \#3 failed"
	failures=1
fi

exit $failures
