# $FreeBSD$
#
# This Makefile differs from the other examples, in the sense that its purpose
# is to install the upstream provided googletest sample unit tests.

# The release package to use for the tests contained within the directory
#
# This applies to components which rely on ^/projects/release-pkg support
# (see UPDATING XXXXXXXXX / svn revision r298107).
PACKAGE=	tests

# Directory into which the Kyuafile provided by this directory will be
# installed.
#
# This is always a subdirectory of ${TESTSBASE}/.  The remainder of the
# path has to match the relative path within the source tree in which
# these files are found modulo the tests/ component at the end.
#
# For example: if this Makefile were in src/bin/cp/tests/, its TESTSDIR
# would point at ${TESTSBASE}/bin/cp/.
TESTSDIR=	${TESTSBASE}/share/examples/tests/googletest

.PATH: ${SRCTOP}/contrib/googletest/googletest/samples

GTEST_MAIN_REQ_TESTS+=	sample1_unittest
GTEST_MAIN_REQ_TESTS+=	sample2_unittest
GTEST_MAIN_REQ_TESTS+=	sample3_unittest
GTEST_MAIN_REQ_TESTS+=	sample4_unittest
GTEST_MAIN_REQ_TESTS+=	sample5_unittest
GTEST_MAIN_REQ_TESTS+=	sample6_unittest
GTEST_MAIN_REQ_TESTS+=	sample7_unittest
GTEST_MAIN_REQ_TESTS+=	sample8_unittest

# sample9_unittest's `CustomOutputTest.Fails` fails intentionally to illustrate
# how output format can be adjusted with command-line parameters.
#GTEST_REQ_TESTS+=	sample9_unittest
GTEST_REQ_TESTS+=	sample10_unittest

# List of test programs to build.  Note that we can build more than one
# test from a single directory, and this is expected.
GTESTS+=		${GTEST_MAIN_REQ_TESTS} ${GTEST_REQ_TESTS}

# 
.for t in ${GTESTS}
.if ${GTEST_MAIN_REQ_TESTS:M$t}
LIBADD.$t+=	gtest_main
.else
LIBADD.$t+=	gtest
.endif
SRCS.$t+=	$t.cc
.endfor

# Additional sources for sample testcase 1, 2, 4, and 5.
SRCS.sample1_unittest+=	sample1.cc
SRCS.sample2_unittest+=	sample2.cc
SRCS.sample4_unittest+=	sample4.cc
SRCS.sample5_unittest+=	sample1.cc

.include <bsd.test.mk>
