#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../../../common.sh

# Description
DESC="Can we traverse up to / and find a 'mk/sys.mk' with -m -C?"

# Run
TEST_N=1
TEST_1="-m .../mk -C ../../../t0/2/1"
TEST_MAKE_DIRS="../../mk 755 ../../../t0/mk 755 ../../../t0/2/1 755"
TEST_COPY_FILES="../../mk/sys.mk 644 ../../../t0/mk/sys.mk 644 ../../../t0/2/1/Makefile.test 644"
TEST_CLEAN_FILES="../../../t0/2/1"
TEST_CLEANUP=clean-special

eval_cmd $*
