#!/bin/sh
# test that the empty file means no pattern
# and an empty pattern means match all.

: ${srcdir=.}

failures=0

for options in '-E' '-E -w' '-F -x' '-G -w -x'; do

	# should return 0 found a match
	echo "" | ${GREP} $options -e '' > /dev/null 2>&1
	if test $? -ne 0 ; then
		echo "Status: Wrong status code, test \#1 failed ($options)"
		failures=1
	fi

	# should return 1 found no match
	echo "abcd" | ${GREP} $options -f /dev/null  > /dev/null 2>&1
	if test $? -ne 1 ; then
		echo "Status: Wrong status code, test \#2 failed ($options)"
		failures=1
	fi

	# should return 0 found a match
	echo "abcd" | ${GREP} $options -f /dev/null -e "abcd" > /dev/null 2>&1
	if test $? -ne 0 ; then
		echo "Status: Wrong status code, test \#3 failed ($options)"
		failures=1
	fi
done

exit $failures
