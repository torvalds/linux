#! /bin/sh
# $FreeBSD$
#
# Copyright 2013 Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * Neither the name of Google Inc. nor the names of its contributors
#   may be used to endorse or promote products derived from this software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# INTRODUCTION
#
# This plain test program mimics the structure and contents of its
# ATF-based counterpart.  It attempts to represent various test cases
# in different separate functions and just calls them all from main.
#
# In reality, plain test programs can be much simpler.  All they have
# to do is return 0 on success and non-0 otherwise.
#

set -e

# Prints an error message and exits.
err() {
	echo "${@}" 1>&2
	exit 1
}

# Auxiliary function to compare two files for equality.
verify_copy() {
	if ! cmp -s "${1}" "${2}"; then
		diff -u "${1}" "${2}"
		err "${1} and ${2} differ, but they should be equal"
	fi
}

simple_test() {
	cp "$(dirname "${0}")/file1" .
	cp file1 file2 || err "cp failed"
	verify_copy file1 file2
}

force_test() {
	echo 'File 3' >file3
	chmod 400 file3
	cp -f file1 file3 || err "cp failed"
	verify_copy file1 file3
}

# If you have read the cp_test.sh counterpart in the atf/ directory, you
# may think that the sequencing of tests below and the exposed behavior
# to the user is very similar.  But you'd be wrong.
#
# There are two major differences with this and the ATF version.  The
# first is that the code below has no provisions to detect failures in
# one test and continue running the other tests: the first failure
# causes the whole test program to exit.  The second is that this
# particular "main" has no arguments: without ATF, all test programs may
# expose a different command-line interface, and this is an issue for
# consistency purposes.
simple_test
force_test
