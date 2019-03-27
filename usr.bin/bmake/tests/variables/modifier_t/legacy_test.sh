#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="Variable expansion with t modifiers"

# Run
TEST_N=3

eval_cmd $*
