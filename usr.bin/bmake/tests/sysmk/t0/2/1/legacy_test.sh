#! /bin/sh
# $FreeBSD$

. $(dirname $0)/../../../../common.sh

# Description
DESC="Can we traverse up to / and find a 'mk/sys.mk'?"

# Run
TEST_N=1
TEST_1="-m .../mk"
TEST_MAKE_DIRS="../../mk 755"
TEST_COPY_FILES="../../mk/sys.mk 644"

eval_cmd $*
