#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="Test semicolon handling."

# Run
TEST_N=2
TEST_1_TODO="parser bug"

eval_cmd $*
