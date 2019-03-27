# $FreeBSD$

.include <src.opts.mk>

# Directory into which the Kyuafile provided by this directory will be
# installed.
#
# This is always a subdirectory of ${TESTSBASE}/.  The remainder of the
# path has to match the relative path within the source tree in which
# these files are found modulo the tests/ component at the end.
#
# For example: if this Makefile were in src/bin/cp/tests/, its TESTSDIR
# would point at ${TESTSBASE}/bin/cp/.
#
# The default path specified by bsd.test.mk is `${TESTSBASE}/${RELDIR:H}`,
# which happens to be the same as `${TESTSBASE}/share/examples/tests`.
#TESTSDIR=	${TESTSBASE}/share/examples/tests

# List of subdirectories into which we want to recurse during the build
# of the system.  We use TESTS_SUBDIRS instead of SUBDIR because we want
# the auto-generated Kyuafile to recurse into these directories.
TESTS_SUBDIRS+=	atf
TESTS_SUBDIRS+=	plain
TESTS_SUBDIRS+=	tap

.if ${MK_GOOGLETEST} != no
TESTS_SUBDIRS+= googletest
.endif

# We leave KYUAFILE unset so that bsd.test.mk auto-generates a Kyuafile
# for us based on the contents of the TESTS_SUBDIRS line above.  The
# generated file will tell the tests run-time engine to recurse into the
# directories listed above.
#KYUAFILE=	auto

.include <bsd.test.mk>
