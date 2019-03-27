# $FreeBSD$

dnl A library of routines for doing regression tests for userland utilities.

dnl Start up.  We initialise the exit status to 0 (no failure) and change
dnl into the directory specified by our first argument, which is the
dnl directory to run the tests inside.
define(`REGRESSION_START',
TESTDIR=$1
if [ -z "$TESTDIR" ]; then
  TESTDIR=.
fi
cd $TESTDIR

STATUS=0)

dnl Check $? to see if we passed or failed.  The first parameter is the test
dnl which passed or failed.  It may be nil.
define(`REGRESSION_PASSFAIL',
if [ $? -eq 0 ]; then
  echo "ok - $1 # Test detected no regression. (in $TESTDIR)"
else
  STATUS=$?
  echo "not ok - $1 # Test failed: regression detected.  See above. (in $TESTDIR)"
fi)

dnl An actual test.  The first parameter is the test name.  The second is the
dnl command/commands to execute for the actual test.  Their exit status is
dnl checked.  It is assumed that the test will output to stdout, and that the
dnl output to be used to check for regression will be in regress.TESTNAME.out.
define(`REGRESSION_TEST',
$2 | diff -u regress.$1.out -
REGRESSION_PASSFAIL($1))

dnl A freeform regression test.  Only exit status is checked.
define(`REGRESSION_TEST_FREEFORM',
$2
REGRESSION_PASSFAIL($1))

dnl A regression test like REGRESSION_TEST, except only regress.out is used
dnl for checking output differences.  The first argument is the command, the
dnl second argument (which may be empty) is the test name.
define(`REGRESSION_TEST_ONE',
$1 | diff -u regress.out -
REGRESSION_PASSFAIL($2))

dnl A fatal error.  This will exit with the given status (first argument) and
dnl print the message (second argument) prefixed with the string "FATAL :" to
dnl the error stream.
define(`REGRESSION_FATAL',
echo "Bail out! $2 (in $TESTDIR)" > /dev/stderr
exit $1)

dnl Cleanup.  Exit with the status code of the last failure.  Should probably
dnl be the number of failed tests, but hey presto, this is what it does.  This
dnl could also clean up potential droppings, if some forms of regression tests
dnl end up using mktemp(1) or such.
define(`REGRESSION_END',
exit $STATUS)
