# $FreeBSD$

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
TESTSDIR=	${TESTSBASE}/share/examples/tests/tap

# List of test programs to build.  Note that we can build more than one
# test from a single directory, and this is expected.
TAP_TESTS_C=	printf_test
TAP_TESTS_SH=	cp_test

# Tell bsd.test.mk that we are providing a hand-crafted Kyuafile in this
# directory.  We do so because the file in this directory exists for
# documentation purposes.
#
# In general, however, you should NOT define KYUAFILE at all to allow
# bsd.test.mk auto-generate one for you based on the PLAIN_TESTS_*
# definitions from above.
KYUAFILE=	yes

# Install file1 and file2 as files via bsd.prog.mk. Please note the intentional
# ${PACKAGE} namespace of files.
#
# The basic semantics of this are the same as FILES in bsd.prog.mk, e.g. the
# installation of the files can be manipulated via ${PACKAGE}FILESDIR,
# ${PACKAGE}FILESMODE, etc.
#
# Please see comment above about ${PACKAGE}. Feel free to omit the ${PACKAGE}
# namespace if release package support isn't needed.
${PACKAGE}FILES+=	file1
CLEANFILES+=		file1

file1:
	@echo "File 1" > ${.TARGET}

.include <bsd.test.mk>
