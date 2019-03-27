# $FreeBSD$
#
# SPDX-License-Identifier: BSD-3-Clause
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
# This sample test program implements various test cases for the cp(1)
# utility in order to demonstrate the usage of the ATF shell API (see
# atf-sh-api(3)).
#

#
# Auxiliary function to compare two files for equality.
#
verify_copy() {
	if ! cmp -s "${1}" "${2}"; then
		echo "${1} and ${2} differ, but they should be equal"
		diff -u "${1}" "${2}"
		atf_fail "Original and copy do not match"
	fi
}

#
# This is the simplest form of a test case definition: a test case
# without a header.
#
# In most cases, this is the definition you will want to use.  However,
# make absolutely sure that the test case name is descriptive enough.
# Multi-word test case names are encouraged.  Keep in mind that these
# are exposed to the reader in the test reports, and the goal is for
# the combination of the test program plus the name of the test case to
# give a pretty clear idea of what specific condition the test is
# validating.
#
atf_test_case simple
simple_body() {
	cp $(atf_get_srcdir)/file1 .

	# The atf_check function is a very powerful function of atf-sh.
	# It allows you to define checkers for the exit status, the
	# stdout and the stderr of any command you execute.  If the
	# result of the command does not match the expectations defined
	# in the checkers, the test fails and verbosely reports data
	# behind the problem.
	#
	# See atf-check(1) for details.
	atf_check -s exit:0 -o empty -e empty cp file1 file2

	verify_copy file1 file2

	# Of special note here is that we are NOT deleting the temporary
	# files we created in this test.  Kyua takes care of this
	# cleanup automatically and tests can (and should) rely on this
	# behavior.
}

#
# This is a more complex form of a test case definition: a test case
# with a header and a body.  You should always favor the simpler
# definition above unless you have to override specific metadata
# variables.
#
# See atf-test-case(4) and kyua-atf-interface(1) for details on all
# available properties.
#
atf_test_case force
force_head() {
	# In this specific case, we define a textual description for
	# the test case, which is later exported to the reports for
	# documentation purposes.
	#
	# However, note again that you should favor highly descriptive
	# test case names to textual descriptions.
	atf_set "descr" "Tests that the -f flag causes cp to forcibly" \
	    "override the destination file"
}
force_body() {
	cp $(atf_get_srcdir)/file1 .
	echo 'File 2' >file2
	chmod 400 file2
	atf_check cp -f file1 file2
	verify_copy file1 file2
}

#
# Lastly, we tell ATF which test cases exist in this program.  This
# function should not do anything other than this registration.
#
atf_init_test_cases() {
	atf_add_test_case simple
	atf_add_test_case force
}
