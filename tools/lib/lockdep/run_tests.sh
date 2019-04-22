#! /bin/bash
# SPDX-License-Identifier: GPL-2.0

if ! make >/dev/null; then
    echo "Building liblockdep failed."
    echo "FAILED!"
    exit 1
fi

find tests -name '*.c' | sort | while read -r i; do
	testname=$(basename "$i" .c)
	echo -ne "$testname... "
	if gcc -o "tests/$testname" -pthread "$i" liblockdep.a -Iinclude -D__USE_LIBLOCKDEP &&
		timeout 1 "tests/$testname" 2>&1 | /bin/bash "tests/${testname}.sh"; then
		echo "PASSED!"
	else
		echo "FAILED!"
	fi
	rm -f "tests/$testname"
done

find tests -name '*.c' | sort | while read -r i; do
	testname=$(basename "$i" .c)
	echo -ne "(PRELOAD) $testname... "
	if gcc -o "tests/$testname" -pthread -Iinclude "$i" &&
		timeout 1 ./lockdep "tests/$testname" 2>&1 |
		/bin/bash "tests/${testname}.sh"; then
		echo "PASSED!"
	else
		echo "FAILED!"
	fi
	rm -f "tests/$testname"
done

find tests -name '*.c' | sort | while read -r i; do
	testname=$(basename "$i" .c)
	echo -ne "(PRELOAD + Valgrind) $testname... "
	if gcc -o "tests/$testname" -pthread -Iinclude "$i" &&
		{ timeout 10 valgrind --read-var-info=yes ./lockdep "./tests/$testname" >& "tests/${testname}.vg.out"; true; } &&
		/bin/bash "tests/${testname}.sh" < "tests/${testname}.vg.out" &&
		! grep -Eq '(^==[0-9]*== (Invalid |Uninitialised ))|Mismatched free|Source and destination overlap| UME ' "tests/${testname}.vg.out"; then
		echo "PASSED!"
	else
		echo "FAILED!"
	fi
	rm -f "tests/$testname"
done
