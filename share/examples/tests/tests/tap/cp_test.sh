#!/bin/sh
#
# Copyright (c) 2017 Ngie Cooper <ngie@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

#
# INTRODUCTION
#
# This TAP test program mimics the structure and contents of its
# ATF-based counterpart.  It attempts to represent various test cases
# in different separate functions and just calls them all from main.
#

test_num=1
TEST_COUNT=4

result()
{
	local result=$1; shift
	local result_string

	result_string="$result $test_num"
	if [ $# -gt 0 ]; then
		result_string="$result_string - $@"
	fi
	echo "$result_string"
	: $(( test_num += 1 ))
}

# Auxiliary function to compare two files for equality.
verify_copy() {
	if cmp -s "${1}" "${2}"; then
		result "ok"
	else
		result "not ok" "${1} and ${2} differ, but they should be equal"
		diff -u "${1}" "${2}"
	fi
}

simple_test() {
	cp "$(dirname "${0}")/file1" .
	if cp file1 file2; then
		result "ok"
		verify_copy file1 file2
	else
		result "not ok" "cp failed"
		result "not ok" "# SKIP"
	fi
}

force_test() {
	echo 'File 3' >file3
	chmod 400 file3
	if cp -f file1 file3; then
		result "ok"
		verify_copy file1 file3
	else
		result "not ok" "cp -f failed"
		result "not ok" "# SKIP"
	fi
}

# If you have read the cp_test.sh counterpart in the atf/ directory, you
# may think that the sequencing of tests below and the exposed behavior
# to the user is very similar.  But you'd be wrong.
#
# There are two major differences with this and the ATF version. First off,
# the TAP test doesn't isolate simple_test from force_test, whereas the ATF
# version does. Secondly, the test script accepts arbitrary command line
# inputs.
echo "1..$TEST_COUNT"

simple_test
force_test
exit 0
