#!/bin/sh
# Test for status code for GNU grep.
# status code
#  0 match found
#  1 no match
#  2 file not found

: ${srcdir=.}

failures=0

# should return 0 found a match
echo "abcd" | ${GREP} -E -e 'abc' > /dev/null 2>&1
if test $? -ne 0 ; then
        echo "Status: Wrong status code, test \#1 failed"
        failures=1
fi

# should return 1 found no match
echo "abcd" | ${GREP} -E -e 'zbc' > /dev/null 2>&1
if test $? -ne 1 ; then
        echo "Status: Wrong status code, test \#2 failed"
        failures=1
fi

# the filename MMMMMMMM.MMM should not exist hopefully
if test -r MMMMMMMM.MMM; then
	echo "Please remove MMMMMMMM.MMM to run check"
else
	# should return 2 file not found
	${GREP} -E -e 'abc' MMMMMMMM.MMM > /dev/null 2>&1
	if test $? -ne 2 ; then
        	echo "Status: Wrong status code, test \#3 failed"
        	failures=1
	fi

	# should return 2 file not found
	${GREP} -E -s -e 'abc' MMMMMMMM.MMM > /dev/null 2>&1
	if test $? -ne 2 ; then
        	echo "Status: Wrong status code, test \#4 failed"
        	failures=1
	fi

	# should return 0 found a match
	echo "abcd" | ${GREP} -E -q -s 'abc' MMMMMMMM.MMM - > /dev/null 2>&1
	if test $? -ne 0 ; then
		echo "Status: Wrong status code, test \#5 failed"
		failures=1
	fi
fi

exit $failures
