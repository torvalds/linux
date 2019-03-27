#!/bin/sh
# $FreeBSD$

i=1
# Temporarily disable evfilt_proc tests: https://bugs.freebsd.org/233586
"$(dirname $0)/kqtest" --no-proc | while read line; do
	echo $line | grep -q passed
	if [ $? -eq 0 ]; then
		echo "ok - $i $line"
		: $(( i += 1 ))
	fi

	echo $line | grep -q 'tests completed'
	if [ $? -eq 0 ]; then
		echo -n "1.."
		echo $line | cut -d' ' -f3
	fi
done
