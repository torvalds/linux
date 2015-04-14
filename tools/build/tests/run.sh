#!/bin/sh

function test_ex {
	make -C ex V=1 clean > ex.out 2>&1
	make -C ex V=1 >> ex.out 2>&1

	if [ ! -x ./ex/ex ]; then
	  echo FAILED
	  exit -1
	fi

	make -C ex V=1 clean > /dev/null 2>&1
	rm -f ex.out
}

function test_ex_suffix {
	make -C ex V=1 clean > ex.out 2>&1

	# use -rR to disable make's builtin rules
	make -rR -C ex V=1 ex.o >> ex.out 2>&1
	make -rR -C ex V=1 ex.i >> ex.out 2>&1
	make -rR -C ex V=1 ex.s >> ex.out 2>&1

	if [ -x ./ex/ex ]; then
	  echo FAILED
	  exit -1
	fi

	if [ ! -f ./ex/ex.o -o ! -f ./ex/ex.i -o ! -f ./ex/ex.s ]; then
	  echo FAILED
	  exit -1
	fi

	make -C ex V=1 clean > /dev/null 2>&1
	rm -f ex.out
}
echo -n Testing..

test_ex
test_ex_suffix

echo OK
