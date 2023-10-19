#!/bin/bash

TCID="safesetid-test.sh"
errcode=0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

check_root()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $TCID: must be run as root >&2
		exit $ksft_skip
	fi
}

main_function()
{
  check_root
  ./safesetid-test
}

main_function
echo "$TCID: done"
exit $errcode
