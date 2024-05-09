#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# kselftest_deps.sh
#
# Checks for kselftest build dependencies on the build system.
# Copyright (c) 2020 Shuah Khan <skhan@linuxfoundation.org>
#
#

usage()
{

echo -e "Usage: $0 -[p] <compiler> [test_name]\n"
echo -e "\tkselftest_deps.sh [-p] gcc"
echo -e "\tkselftest_deps.sh [-p] gcc mm"
echo -e "\tkselftest_deps.sh [-p] aarch64-linux-gnu-gcc"
echo -e "\tkselftest_deps.sh [-p] aarch64-linux-gnu-gcc mm\n"
echo "- Should be run in selftests directory in the kernel repo."
echo "- Checks if Kselftests can be built/cross-built on a system."
echo "- Parses all test/sub-test Makefile to find library dependencies."
echo "- Runs compile test on a trivial C file with LDLIBS specified"
echo "  in the test Makefiles to identify missing library dependencies."
echo "- Prints suggested target list for a system filtering out tests"
echo "  failed the build dependency check from the TARGETS in Selftests"
echo "  main Makefile when optional -p is specified."
echo "- Prints pass/fail dependency check for each tests/sub-test."
echo "- Prints pass/fail targets and libraries."
echo "- Default: runs dependency checks on all tests."
echo "- Optional: test name can be specified to check dependencies for it."
exit 1

}

# Start main()
main()
{

base_dir=`pwd`
# Make sure we're in the selftests top-level directory.
if [ $(basename "$base_dir") !=  "selftests" ]; then
	echo -e "\tPlease run $0 in"
	echo -e "\ttools/testing/selftests directory ..."
	exit 1
fi

print_targets=0

while getopts "p" arg; do
	case $arg in
		p)
		print_targets=1
	shift;;
	esac
done

if [ $# -eq 0 ]
then
	usage
fi

# Compiler
CC=$1

tmp_file=$(mktemp).c
trap "rm -f $tmp_file.o $tmp_file $tmp_file.bin" EXIT
#echo $tmp_file

pass=$(mktemp).out
trap "rm -f $pass" EXIT
#echo $pass

fail=$(mktemp).out
trap "rm -f $fail" EXIT
#echo $fail

# Generate tmp source fire for compile test
cat << "EOF" > $tmp_file
int main()
{
}
EOF

# Save results
total_cnt=0
fail_trgts=()
fail_libs=()
fail_cnt=0
pass_trgts=()
pass_libs=()
pass_cnt=0

# Get all TARGETS from selftests Makefile
targets=$(grep -E "^TARGETS +|^TARGETS =" Makefile | cut -d "=" -f2)

# Initially, in LDLIBS related lines, the dep checker needs
# to ignore lines containing the following strings:
filter="\$(VAR_LDLIBS)\|pkg-config\|PKG_CONFIG\|IOURING_EXTRA_LIBS"

# Single test case
if [ $# -eq 2 ]
then
	test=$2/Makefile

	l1_test $test
	l2_test $test
	l3_test $test
	l4_test $test
	l5_test $test

	print_results $1 $2
	exit $?
fi

# Level 1: LDLIBS set static.
#
# Find all LDLIBS set statically for all executables built by a Makefile
# and filter out VAR_LDLIBS to discard the following:
# 	gpio/Makefile:LDLIBS += $(VAR_LDLIBS)
# Append space at the end of the list to append more tests.

l1_tests=$(grep -r --include=Makefile "^LDLIBS" | \
		grep -v "$filter" | awk -F: '{print $1}' | uniq)

# Level 2: LDLIBS set dynamically.
#
# Level 2
# Some tests have multiple valid LDLIBS lines for individual sub-tests
# that need dependency checks. Find them and append them to the tests
# e.g: mm/Makefile:$(OUTPUT)/userfaultfd: LDLIBS += -lpthread
# Filter out VAR_LDLIBS to discard the following:
# 	memfd/Makefile:$(OUTPUT)/fuse_mnt: LDLIBS += $(VAR_LDLIBS)
# Append space at the end of the list to append more tests.

l2_tests=$(grep -r --include=Makefile ": LDLIBS" | \
		grep -v "$filter" | awk -F: '{print $1}' | uniq)

# Level 3
# memfd and others use pkg-config to find mount and fuse libs
# respectively and save it in VAR_LDLIBS. If pkg-config doesn't find
# any, VAR_LDLIBS set to default.
# Use the default value and filter out pkg-config for dependency check.
# e.g:
# memfd/Makefile
#	VAR_LDLIBS := $(shell pkg-config fuse --libs 2>/dev/null)

l3_tests=$(grep -r --include=Makefile "^VAR_LDLIBS" | \
		grep -v "pkg-config\|PKG_CONFIG" | awk -F: '{print $1}' | uniq)

# Level 4
# some tests may fall back to default using `|| echo -l<libname>`
# if pkg-config doesn't find the libs, instead of using VAR_LDLIBS
# as per level 3 checks.
# e.g:
# netfilter/Makefile
#	LDLIBS += $(shell $(HOSTPKG_CONFIG) --libs libmnl 2>/dev/null || echo -lmnl)
l4_tests=$(grep -r --include=Makefile "^LDLIBS" | \
		grep "pkg-config\|PKG_CONFIG" | awk -F: '{print $1}' | uniq)

# Level 5
# some tests may use IOURING_EXTRA_LIBS to add extra libs to LDLIBS,
# which in turn may be defined in a sub-Makefile
# e.g.:
# mm/Makefile
#	$(OUTPUT)/gup_longterm: LDLIBS += $(IOURING_EXTRA_LIBS)
l5_tests=$(grep -r --include=Makefile "LDLIBS +=.*\$(IOURING_EXTRA_LIBS)" | \
	awk -F: '{print $1}' | uniq)

#echo l1_tests $l1_tests
#echo l2_tests $l2_tests
#echo l3_tests $l3_tests
#echo l4_tests $l4_tests
#echo l5_tests $l5_tests

all_tests
print_results $1 $2

exit $?
}
# end main()

all_tests()
{
	for test in $l1_tests; do
		l1_test $test
	done

	for test in $l2_tests; do
		l2_test $test
	done

	for test in $l3_tests; do
		l3_test $test
	done

	for test in $l4_tests; do
		l4_test $test
	done

	for test in $l5_tests; do
		l5_test $test
	done
}

# Use same parsing used for l1_tests and pick libraries this time.
l1_test()
{
	test_libs=$(grep --include=Makefile "^LDLIBS" $test | \
			grep -v "$filter" | \
			sed -e 's/\:/ /' | \
			sed -e 's/+/ /' | cut -d "=" -f 2)

	check_libs $test $test_libs
}

# Use same parsing used for l2_tests and pick libraries this time.
l2_test()
{
	test_libs=$(grep --include=Makefile ": LDLIBS" $test | \
			grep -v "$filter" | \
			sed -e 's/\:/ /' | sed -e 's/+/ /' | \
			cut -d "=" -f 2)

	check_libs $test $test_libs
}

l3_test()
{
	test_libs=$(grep --include=Makefile "^VAR_LDLIBS" $test | \
			grep -v "pkg-config" | sed -e 's/\:/ /' |
			sed -e 's/+/ /' | cut -d "=" -f 2)

	check_libs $test $test_libs
}

l4_test()
{
	test_libs=$(grep --include=Makefile "^VAR_LDLIBS\|^LDLIBS" $test | \
			grep "\(pkg-config\|PKG_CONFIG\).*|| echo " | \
			sed -e 's/.*|| echo //' | sed -e 's/)$//')

	check_libs $test $test_libs
}

l5_test()
{
	tests=$(find $(dirname "$test") -type f -name "*.mk")
	test_libs=$(grep "^IOURING_EXTRA_LIBS +\?=" $tests | \
			cut -d "=" -f 2)

	check_libs $test $test_libs
}

check_libs()
{

if [[ ! -z "${test_libs// }" ]]
then

	#echo $test_libs

	for lib in $test_libs; do

	let total_cnt+=1
	$CC -o $tmp_file.bin $lib $tmp_file > /dev/null 2>&1
	if [ $? -ne 0 ]; then
		echo "FAIL: $test dependency check: $lib" >> $fail
		let fail_cnt+=1
		fail_libs+="$lib "
		fail_target=$(echo "$test" | cut -d "/" -f1)
		fail_trgts+="$fail_target "
		targets=$(echo "$targets" | grep -v "$fail_target")
	else
		echo "PASS: $test dependency check passed $lib" >> $pass
		let pass_cnt+=1
		pass_libs+="$lib "
		pass_trgts+="$(echo "$test" | cut -d "/" -f1) "
	fi

	done
fi
}

print_results()
{
	echo -e "========================================================";
	echo -e "Kselftest Dependency Check for [$0 $1 $2] results..."

	if [ $print_targets -ne 0 ]
	then
	echo -e "Suggested Selftest Targets for your configuration:"
	echo -e "$targets";
	fi

	echo -e "========================================================";
	echo -e "Checked tests defining LDLIBS dependencies"
	echo -e "--------------------------------------------------------";
	echo -e "Total tests with Dependencies:"
	echo -e "$total_cnt Pass: $pass_cnt Fail: $fail_cnt";

	if [ $pass_cnt -ne 0 ]; then
	echo -e "--------------------------------------------------------";
	cat $pass
	echo -e "--------------------------------------------------------";
	echo -e "Targets passed build dependency check on system:"
	echo -e "$(echo "$pass_trgts" | xargs -n1 | sort -u | xargs)"
	fi

	if [ $fail_cnt -ne 0 ]; then
	echo -e "--------------------------------------------------------";
	cat $fail
	echo -e "--------------------------------------------------------";
	echo -e "Targets failed build dependency check on system:"
	echo -e "$(echo "$fail_trgts" | xargs -n1 | sort -u | xargs)"
	echo -e "--------------------------------------------------------";
	echo -e "Missing libraries system"
	echo -e "$(echo "$fail_libs" | xargs -n1 | sort -u | xargs)"
	fi

	echo -e "--------------------------------------------------------";
	echo -e "========================================================";
}

main "$@"
