#!/bin/sh
# $FreeBSD$

base=`basename $0`

echo "1..3"

assert_equals() {
    testnum="$1"
    expected="$2"
    fn="$3"
    if [ "$expected" = "$($fn)" ]
    then
        echo "ok $testnum - $fn"
    else
        echo "not ok $testnum - $fn"
    fi
}

assert_equals 1 1 "units -t ft ft"
assert_equals 2 12 "units -t ft in"
assert_equals 3 0.083333333 "units -t in ft"
