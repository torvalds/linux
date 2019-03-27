# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2017 Alan Somers
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

atf_test_case across
across_head() {
        atf_set "descr" "Format columns in round-robin order with pr -a"
}
across_body() {
	atf_check -s exit:0 -o file:$(atf_get_srcdir)/across.out \
		-x "pr -t -a -2 $(atf_get_srcdir)/other.in"
}

atf_test_case merge
merge_head() {
        atf_set "descr" "Merge two files with pr -m"
}
merge_body() {
	atf_check -s ignore -o file:$(atf_get_srcdir)/merge.out \
		pr -t -m $(atf_get_srcdir)/d_basic.in $(atf_get_srcdir)/other.in
}

atf_test_case threecol
threecol_head() {
        atf_set "descr" "Format a file with three columns"
}
threecol_body() {
	atf_check -s ignore -o file:$(atf_get_srcdir)/threecol.out \
		pr -t -3 $(atf_get_srcdir)/other.in
}

atf_init_test_cases()
{
        atf_add_test_case across
        atf_add_test_case merge
        atf_add_test_case threecol
}
