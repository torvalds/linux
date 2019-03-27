#!/bin/sh
# $FreeBSD$

run() { ../shlib-compat.py --no-dump -vv libtest$1/libtest$1.so.0.full libtest$2/libtest$2.so.0.full; }
echo 1..9
REGRESSION_START($1)
REGRESSION_TEST(`1-1', `run 1 1')
REGRESSION_TEST(`1-2', `run 1 2')
REGRESSION_TEST(`1-3', `run 1 3')
REGRESSION_TEST(`2-1', `run 2 1')
REGRESSION_TEST(`2-2', `run 2 2')
REGRESSION_TEST(`2-3', `run 2 3')
REGRESSION_TEST(`3-1', `run 3 1')
REGRESSION_TEST(`3-2', `run 3 2')
REGRESSION_TEST(`3-3', `run 3 3')
REGRESSION_END()
