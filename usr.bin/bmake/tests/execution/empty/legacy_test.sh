#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="Empty command (from variable)"

# Run
TEST_N=1
TEST_1=

eval_cmd $*
