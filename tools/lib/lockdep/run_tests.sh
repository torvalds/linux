#! /bin/bash

make &> /dev/null

for i in `ls tests/*.c`; do
	testname=$(basename "$i" .c)
	gcc -o tests/$testname -pthread $i liblockdep.a -Iinclude -D__USE_LIBLOCKDEP &> /dev/null
	echo -ne "$testname... "
	if [ $(timeout 1 ./tests/$testname | wc -l) -gt 0 ]; then
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
	if [ $(timeout 1 ./lockdep ./tests/$testname | wc -l) -gt 0 ]; then
		echo "PASSED!"
	else
		echo "FAILED!"
	fi
	if [ -f "tests/$testname" ]; then
		rm tests/$testname
	fi
done
