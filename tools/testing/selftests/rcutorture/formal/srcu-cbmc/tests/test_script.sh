#!/bin/sh

# This script expects a mode (either --should-pass or --should-fail) followed by
# an input file. The script uses the following environment variables. The test C
# source file is expected to be named test.c in the directory containing the
# input file.
#
# CBMC: The command to run CBMC. Default: cbmc
# CBMC_FLAGS: Additional flags to pass to CBMC
# NR_CPUS: Number of cpus to run tests with. Default specified by the test
# SYNC_SRCU_MODE: Choose implementation of synchronize_srcu. Defaults to simple.
#                 kernel: Version included in the linux kernel source.
#                 simple: Use try_check_zero directly.
#
# The input file is a script that is sourced by this file. It can define any of
# the following variables to configure the test.
#
# test_cbmc_options: Extra options to pass to CBMC.
# min_cpus_fail: Minimum number of CPUs (NR_CPUS) for verification to fail.
#                The test is expected to pass if it is run with fewer. (Only
#                useful for .fail files)
# default_cpus: Quantity of CPUs to use for the test, if not specified on the
#               command line. Default: Larger of 2 and MIN_CPUS_FAIL.

set -e

if test "$#" -ne 2; then
	echo "Expected one option followed by an input file" 1>&2
	exit 99
fi

if test "x$1" = "x--should-pass"; then
	should_pass="yes"
elif test "x$1" = "x--should-fail"; then
	should_pass="no"
else
	echo "Unrecognized argument '$1'" 1>&2

	# Exit code 99 indicates a hard error.
	exit 99
fi

CBMC=${CBMC:-cbmc}

SYNC_SRCU_MODE=${SYNC_SRCU_MODE:-simple}

case ${SYNC_SRCU_MODE} in
kernel) sync_srcu_mode_flags="" ;;
simple) sync_srcu_mode_flags="-DUSE_SIMPLE_SYNC_SRCU" ;;

*)
	echo "Unrecognized argument '${SYNC_SRCU_MODE}'" 1>&2
	exit 99
	;;
esac

min_cpus_fail=1

c_file=`dirname "$2"`/test.c

# Source the input file.
. $2

if test ${min_cpus_fail} -gt 2; then
	default_default_cpus=${min_cpus_fail}
else
	default_default_cpus=2
fi
default_cpus=${default_cpus:-${default_default_cpus}}
cpus=${NR_CPUS:-${default_cpus}}

# Check if there are two few cpus to make the test fail.
if test $cpus -lt ${min_cpus_fail:-0}; then
	should_pass="yes"
fi

cbmc_opts="-DNR_CPUS=${cpus} ${sync_srcu_mode_flags} ${test_cbmc_options} ${CBMC_FLAGS}"

echo "Running CBMC: ${CBMC} ${cbmc_opts} ${c_file}"
if ${CBMC} ${cbmc_opts} "${c_file}"; then
	# Verification successful. Make sure that it was supposed to verify.
	test "x${should_pass}" = xyes
else
	cbmc_exit_status=$?

	# An exit status of 10 indicates a failed verification.
	# (see cbmc_parse_optionst::do_bmc in the CBMC source code)
	if test ${cbmc_exit_status} -eq 10 && test "x${should_pass}" = xno; then
		:
	else
		echo "CBMC returned ${cbmc_exit_status} exit status" 1>&2

		# Parse errors have exit status 6. Any other type of error
		# should be considered a hard error.
		if test ${cbmc_exit_status} -ne 6 && \
		   test ${cbmc_exit_status} -ne 10; then
			exit 99
		else
			exit 1
		fi
	fi
fi
