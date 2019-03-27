#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright 2018 (C) Yuri Pankov
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

atf_test_case c_flag
c_flag_head()
{
	atf_set "descr" "Verify -c output (PR 224552)"
}
c_flag_body()
{
	export LC_ALL="en_US.UTF-8"

	printf 'TestTestTestTes\345Test\345' > d_od_cflag.in
	atf_check -o file:"$(atf_get_srcdir)/d_od_cflag_a.out" \
	    od -c d_od_cflag.in
	printf 'TestTestTestTesтТест' > d_od_cflag.in
	atf_check -o file:"$(atf_get_srcdir)/d_od_cflag_b.out" \
	    od -c d_od_cflag.in
}

atf_init_test_cases()
{
	atf_add_test_case c_flag
}
