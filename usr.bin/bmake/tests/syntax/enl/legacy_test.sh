#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="Test escaped new-lines handling."

# Run
TEST_N=5
TEST_2_TODO="bug in parser"

eval_cmd $*
