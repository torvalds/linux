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

atf_test_case special
special_head() {
	atf_set "descr" "Test cmp(1)'s handling of non-regular files"
}
special_body() {
	echo 0123456789abcdef > a
	echo 0123456789abcdeg > b
	atf_check -s exit:0 -o empty -e empty -x "cat a | cmp a -"
	atf_check -s exit:0 -o empty -e empty -x "cat a | cmp - a"
	atf_check -s exit:1 -o not-empty -e empty -x "cat b | cmp a -"
	atf_check -s exit:1 -o not-empty -e empty -x "cat b | cmp - a"

	atf_check -s exit:0 -o empty -e empty -x "cmp a a <&-"
}

atf_test_case symlink
symlink_head() {
	atf_set "descr" "Test cmp(1)'s handling of symlinks"
}
symlink_body() {
	echo 0123456789abcdef > a
	echo 0123456789abcdeg > b
	ln -s a a.lnk
	ln -s b b.lnk
	ln -s a a2.lnk
	cp a adup
	ln -s adup adup.lnk
	atf_check -s exit:0 cmp a a.lnk
	atf_check -s exit:0 cmp a.lnk a
	atf_check -s not-exit:0 -o ignore cmp a b.lnk
	atf_check -s not-exit:0 -o ignore cmp b.lnk a
	atf_check -s not-exit:0 -o ignore -e ignore cmp -h a a.lnk
	atf_check -s not-exit:0 -o ignore -e ignore cmp -h a.lnk a
	atf_check -s exit:0 cmp -h a.lnk a2.lnk
	atf_check -s not-exit:0 -o ignore -e ignore cmp -h a.lnk adup.lnk
}

atf_init_test_cases()
{
	atf_add_test_case special
	atf_add_test_case symlink
}
