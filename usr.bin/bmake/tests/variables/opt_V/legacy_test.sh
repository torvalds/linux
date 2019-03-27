#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="Variable expansion using command line '-V'"

# Run
TEST_N=2

eval_cmd $*
