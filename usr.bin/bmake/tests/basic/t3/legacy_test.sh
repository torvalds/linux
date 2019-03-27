#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../common.sh

# Description
DESC="No Makefile file, no command line target."

# Run
TEST_N=1
TEST_1=

eval_cmd $*
