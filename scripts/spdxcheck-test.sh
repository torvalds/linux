#!/bin/sh

for PYTHON in python2 python3; do
	# run check on a text and a binary file
	for FILE in Makefile Documentation/logo.gif; do
		$PYTHON scripts/spdxcheck.py $FILE
		$PYTHON scripts/spdxcheck.py - < $FILE
	done

	# run check on complete tree to catch any other issues
	$PYTHON scripts/spdxcheck.py > /dev/null
done
