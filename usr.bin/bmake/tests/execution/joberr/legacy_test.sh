#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="Test job make error output"

# Run
TEST_N=1
TEST_1=

eval_cmd $*
