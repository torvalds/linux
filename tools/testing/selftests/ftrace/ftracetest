#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# ftracetest - Ftrace test shell scripts
#
# Copyright (C) Hitachi Ltd., 2014
#  Written by Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
#

usage() { # errno [message]
[ ! -z "$2" ] && echo $2
echo "Usage: ftracetest [options] [testcase(s)] [testcase-directory(s)]"
echo " Options:"
echo "		-h|--help  Show help message"
echo "		-k|--keep  Keep passed test logs"
echo "		-v|--verbose Increase verbosity of test messages"
echo "		-vv        Alias of -v -v (Show all results in stdout)"
echo "		-vvv       Alias of -v -v -v (Show all commands immediately)"
echo "		--fail-unsupported Treat UNSUPPORTED as a failure"
echo "		--fail-unresolved Treat UNRESOLVED as a failure"
echo "		-d|--debug Debug mode (trace all shell commands)"
echo "		-l|--logdir <dir> Save logs on the <dir>"
echo "		            If <dir> is -, all logs output in console only"
exit $1
}

# default error
err_ret=1

# kselftest skip code is 4
err_skip=4

# cgroup RT scheduling prevents chrt commands from succeeding, which
# induces failures in test wakeup tests.  Disable for the duration of
# the tests.

readonly sched_rt_runtime=/proc/sys/kernel/sched_rt_runtime_us

sched_rt_runtime_orig=$(cat $sched_rt_runtime)

setup() {
  echo -1 > $sched_rt_runtime
}

cleanup() {
  echo $sched_rt_runtime_orig > $sched_rt_runtime
}

errexit() { # message
  echo "Error: $1" 1>&2
  cleanup
  exit $err_ret
}

# Ensuring user privilege
if [ `id -u` -ne 0 ]; then
  errexit "this must be run by root user"
fi

setup

# Utilities
absdir() { # file_path
  (cd `dirname $1`; pwd)
}

abspath() {
  echo `absdir $1`/`basename $1`
}

find_testcases() { #directory
  echo `find $1 -name \*.tc | sort`
}

parse_opts() { # opts
  local OPT_TEST_CASES=
  local OPT_TEST_DIR=

  while [ ! -z "$1" ]; do
    case "$1" in
    --help|-h)
      usage 0
    ;;
    --keep|-k)
      KEEP_LOG=1
      shift 1
    ;;
    --verbose|-v|-vv|-vvv)
      if [ $VERBOSE -eq -1 ]; then
	usage "--console can not use with --verbose"
      fi
      VERBOSE=$((VERBOSE + 1))
      [ $1 = '-vv' ] && VERBOSE=$((VERBOSE + 1))
      [ $1 = '-vvv' ] && VERBOSE=$((VERBOSE + 2))
      shift 1
    ;;
    --console)
      if [ $VERBOSE -ne 0 ]; then
	usage "--console can not use with --verbose"
      fi
      VERBOSE=-1
      shift 1
    ;;
    --debug|-d)
      DEBUG=1
      shift 1
    ;;
    --stop-fail)
      STOP_FAILURE=1
      shift 1
    ;;
    --fail-unsupported)
      UNSUPPORTED_RESULT=1
      shift 1
    ;;
    --fail-unresolved)
      UNRESOLVED_RESULT=1
      shift 1
    ;;
    --logdir|-l)
      LOG_DIR=$2
      shift 2
    ;;
    *.tc)
      if [ -f "$1" ]; then
        OPT_TEST_CASES="$OPT_TEST_CASES `abspath $1`"
        shift 1
      else
        usage 1 "$1 is not a testcase"
      fi
      ;;
    *)
      if [ -d "$1" ]; then
        OPT_TEST_DIR=`abspath $1`
        OPT_TEST_CASES="$OPT_TEST_CASES `find_testcases $OPT_TEST_DIR`"
        shift 1
      else
        usage 1 "Invalid option ($1)"
      fi
    ;;
    esac
  done
  if [ ! -z "$OPT_TEST_CASES" ]; then
    TEST_CASES=$OPT_TEST_CASES
  fi
}

# Parameters
TRACING_DIR=`grep tracefs /proc/mounts | cut -f2 -d' ' | head -1`
if [ -z "$TRACING_DIR" ]; then
    DEBUGFS_DIR=`grep debugfs /proc/mounts | cut -f2 -d' ' | head -1`
    if [ -z "$DEBUGFS_DIR" ]; then
	# If tracefs exists, then so does /sys/kernel/tracing
	if [ -d "/sys/kernel/tracing" ]; then
	    mount -t tracefs nodev /sys/kernel/tracing ||
	      errexit "Failed to mount /sys/kernel/tracing"
	    TRACING_DIR="/sys/kernel/tracing"
	# If debugfs exists, then so does /sys/kernel/debug
	elif [ -d "/sys/kernel/debug" ]; then
	    mount -t debugfs nodev /sys/kernel/debug ||
	      errexit "Failed to mount /sys/kernel/debug"
	    TRACING_DIR="/sys/kernel/debug/tracing"
	else
	    err_ret=$err_skip
	    errexit "debugfs and tracefs are not configured in this kernel"
	fi
    else
	TRACING_DIR="$DEBUGFS_DIR/tracing"
    fi
fi
if [ ! -d "$TRACING_DIR" ]; then
    err_ret=$err_skip
    errexit "ftrace is not configured in this kernel"
fi

TOP_DIR=`absdir $0`
TEST_DIR=$TOP_DIR/test.d
TEST_CASES=`find_testcases $TEST_DIR`
LOG_DIR=$TOP_DIR/logs/`date +%Y%m%d-%H%M%S`/
KEEP_LOG=0
DEBUG=0
VERBOSE=0
UNSUPPORTED_RESULT=0
UNRESOLVED_RESULT=0
STOP_FAILURE=0
# Parse command-line options
parse_opts $*

[ $DEBUG -ne 0 ] && set -x

# Verify parameters
if [ -z "$TRACING_DIR" -o ! -d "$TRACING_DIR" ]; then
  errexit "No ftrace directory found"
fi

# Preparing logs
if [ "x$LOG_DIR" = "x-" ]; then
  LOG_FILE=
  date
else
  LOG_FILE=$LOG_DIR/ftracetest.log
  mkdir -p $LOG_DIR || errexit "Failed to make a log directory: $LOG_DIR"
  date > $LOG_FILE
fi

# Define text colors
# Check available colors on the terminal, if any
ncolors=`tput colors 2>/dev/null || echo 0`
color_reset=
color_red=
color_green=
color_blue=
# If stdout exists and number of colors is eight or more, use them
if [ -t 1 -a "$ncolors" -ge 8 ]; then
  color_reset="\033[0m"
  color_red="\033[31m"
  color_green="\033[32m"
  color_blue="\033[34m"
fi

strip_esc() {
  # busybox sed implementation doesn't accept "\x1B", so use [:cntrl:] instead.
  sed -E "s/[[:cntrl:]]\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g"
}

prlog() { # messages
  newline="\n"
  if [ "$1" = "-n" ] ; then
    newline=
    shift
  fi
  printf "$*$newline"
  [ "$LOG_FILE" ] && printf "$*$newline" | strip_esc >> $LOG_FILE
}
catlog() { #file
  cat $1
  [ "$LOG_FILE" ] && cat $1 | strip_esc >> $LOG_FILE
}
prlog "=== Ftrace unit tests ==="


# Testcase management
# Test result codes - Dejagnu extended code
PASS=0	# The test succeeded.
FAIL=1	# The test failed, but was expected to succeed.
UNRESOLVED=2  # The test produced indeterminate results. (e.g. interrupted)
UNTESTED=3    # The test was not run, currently just a placeholder.
UNSUPPORTED=4 # The test failed because of lack of feature.
XFAIL=5	# The test failed, and was expected to fail.

# Accumulations
PASSED_CASES=
FAILED_CASES=
UNRESOLVED_CASES=
UNTESTED_CASES=
UNSUPPORTED_CASES=
XFAILED_CASES=
UNDEFINED_CASES=
TOTAL_RESULT=0

INSTANCE=
CASENO=0

testcase() { # testfile
  CASENO=$((CASENO+1))
  desc=`grep "^#[ \t]*description:" $1 | cut -f2- -d:`
  prlog -n "[$CASENO]$INSTANCE$desc"
}

checkreq() { # testfile
  requires=`grep "^#[ \t]*requires:" $1 | cut -f2- -d:`
  # Use eval to pass quoted-patterns correctly.
  eval check_requires "$requires"
}

test_on_instance() { # testfile
  grep -q "^#[ \t]*flags:.*instance" $1
}

eval_result() { # sigval
  case $1 in
    $PASS)
      prlog "	[${color_green}PASS${color_reset}]"
      PASSED_CASES="$PASSED_CASES $CASENO"
      return 0
    ;;
    $FAIL)
      prlog "	[${color_red}FAIL${color_reset}]"
      FAILED_CASES="$FAILED_CASES $CASENO"
      return 1 # this is a bug.
    ;;
    $UNRESOLVED)
      prlog "	[${color_blue}UNRESOLVED${color_reset}]"
      UNRESOLVED_CASES="$UNRESOLVED_CASES $CASENO"
      return $UNRESOLVED_RESULT # depends on use case
    ;;
    $UNTESTED)
      prlog "	[${color_blue}UNTESTED${color_reset}]"
      UNTESTED_CASES="$UNTESTED_CASES $CASENO"
      return 0
    ;;
    $UNSUPPORTED)
      prlog "	[${color_blue}UNSUPPORTED${color_reset}]"
      UNSUPPORTED_CASES="$UNSUPPORTED_CASES $CASENO"
      return $UNSUPPORTED_RESULT # depends on use case
    ;;
    $XFAIL)
      prlog "	[${color_green}XFAIL${color_reset}]"
      XFAILED_CASES="$XFAILED_CASES $CASENO"
      return 0
    ;;
    *)
      prlog "	[${color_blue}UNDEFINED${color_reset}]"
      UNDEFINED_CASES="$UNDEFINED_CASES $CASENO"
      return 1 # this must be a test bug
    ;;
  esac
}

# Signal handling for result codes
SIG_RESULT=
SIG_BASE=36	# Use realtime signals
SIG_PID=$$

exit_pass () {
  exit 0
}

SIG_FAIL=$((SIG_BASE + FAIL))
exit_fail () {
  exit 1
}
trap 'SIG_RESULT=$FAIL' $SIG_FAIL

SIG_UNRESOLVED=$((SIG_BASE + UNRESOLVED))
exit_unresolved () {
  kill -s $SIG_UNRESOLVED $SIG_PID
  exit 0
}
trap 'SIG_RESULT=$UNRESOLVED' $SIG_UNRESOLVED

SIG_UNTESTED=$((SIG_BASE + UNTESTED))
exit_untested () {
  kill -s $SIG_UNTESTED $SIG_PID
  exit 0
}
trap 'SIG_RESULT=$UNTESTED' $SIG_UNTESTED

SIG_UNSUPPORTED=$((SIG_BASE + UNSUPPORTED))
exit_unsupported () {
  kill -s $SIG_UNSUPPORTED $SIG_PID
  exit 0
}
trap 'SIG_RESULT=$UNSUPPORTED' $SIG_UNSUPPORTED

SIG_XFAIL=$((SIG_BASE + XFAIL))
exit_xfail () {
  kill -s $SIG_XFAIL $SIG_PID
  exit 0
}
trap 'SIG_RESULT=$XFAIL' $SIG_XFAIL

__run_test() { # testfile
  # setup PID and PPID, $$ is not updated.
  (cd $TRACING_DIR; read PID _ < /proc/self/stat; set -e; set -x;
   checkreq $1; initialize_ftrace; . $1)
  [ $? -ne 0 ] && kill -s $SIG_FAIL $SIG_PID
}

# Run one test case
run_test() { # testfile
  local testname=`basename $1`
  testcase $1
  if [ ! -z "$LOG_FILE" ] ; then
    local testlog=`mktemp $LOG_DIR/${CASENO}-${testname}-log.XXXXXX`
  else
    local testlog=/proc/self/fd/1
  fi
  export TMPDIR=`mktemp -d /tmp/ftracetest-dir.XXXXXX`
  export FTRACETEST_ROOT=$TOP_DIR
  echo "execute$INSTANCE: "$1 > $testlog
  SIG_RESULT=0
  if [ $VERBOSE -eq -1 ]; then
    __run_test $1
  elif [ -z "$LOG_FILE" ]; then
    __run_test $1 2>&1
  elif [ $VERBOSE -ge 3 ]; then
    __run_test $1 | tee -a $testlog 2>&1
  elif [ $VERBOSE -eq 2 ]; then
    __run_test $1 2>> $testlog | tee -a $testlog
  else
    __run_test $1 >> $testlog 2>&1
  fi
  eval_result $SIG_RESULT
  if [ $? -eq 0 ]; then
    # Remove test log if the test was done as it was expected.
    [ $KEEP_LOG -eq 0 -a ! -z "$LOG_FILE" ] && rm $testlog
  else
    [ $VERBOSE -eq 1 -o $VERBOSE -eq 2 ] && catlog $testlog
    TOTAL_RESULT=1
  fi
  rm -rf $TMPDIR
}

# load in the helper functions
. $TEST_DIR/functions

# Main loop
for t in $TEST_CASES; do
  run_test $t
  if [ $STOP_FAILURE -ne 0 -a $TOTAL_RESULT -ne 0 ]; then
    echo "A failure detected. Stop test."
    exit 1
  fi
done

# Test on instance loop
INSTANCE=" (instance) "
for t in $TEST_CASES; do
  test_on_instance $t || continue
  SAVED_TRACING_DIR=$TRACING_DIR
  export TRACING_DIR=`mktemp -d $TRACING_DIR/instances/ftracetest.XXXXXX`
  run_test $t
  rmdir $TRACING_DIR
  TRACING_DIR=$SAVED_TRACING_DIR
  if [ $STOP_FAILURE -ne 0 -a $TOTAL_RESULT -ne 0 ]; then
    echo "A failure detected. Stop test."
    exit 1
  fi
done
(cd $TRACING_DIR; initialize_ftrace) # for cleanup

prlog ""
prlog "# of passed: " `echo $PASSED_CASES | wc -w`
prlog "# of failed: " `echo $FAILED_CASES | wc -w`
prlog "# of unresolved: " `echo $UNRESOLVED_CASES | wc -w`
prlog "# of untested: " `echo $UNTESTED_CASES | wc -w`
prlog "# of unsupported: " `echo $UNSUPPORTED_CASES | wc -w`
prlog "# of xfailed: " `echo $XFAILED_CASES | wc -w`
prlog "# of undefined(test bug): " `echo $UNDEFINED_CASES | wc -w`

cleanup

# if no error, return 0
exit $TOTAL_RESULT
