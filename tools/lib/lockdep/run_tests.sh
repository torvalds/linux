#! /bin/bash
# SPDX-License-Identifier: GPL-2.0

make &> /dev/null

for i in `ls tests/*.c`; do
	testname=$(basename "$i" .c)
	gcc -o tests/$testname -pthread $i liblockdep.a -Iinclude -D__USE_LIBLOCKDEP &> /dev/null
	echo -ne "$testname... "
	if [ $(timeout 1 ./tests/$testname 2>&1 | wc -l) -gt 0 ]; then
		echo "PASSED!"
	else
		echo "FAILED!"
	fi
	if [ -f "tests/$testname" ]; then
		rm tests/$testname
	fi
done

for i in `ls tests/*.c`; do
	testname=$(basename "$i" .c)
	gcc -o tests/$testname -pthread -Iinclude $i &> /dev/null
	echo -ne "(PRELOAD) $testname... "
	if [ $(timeout 1 ./lockdep ./tests/$testname 2>&1 | wc -l) -gt 0 ]; then
		echo "PASSED!"
	else
		echo "FAILED!"
	fi
	if [ -f "tests/$testname" ]; then
		rm tests/$testname
	fi
done
