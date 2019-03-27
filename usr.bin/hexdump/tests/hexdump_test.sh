#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2017 Kyle Evans <kevans@FreeBSD.org>
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

atf_test_case b_flag
b_flag_head()
{
	atf_set "descr" "Verify -b output"
}
b_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_bflag_a.out" \
	    hexdump -b "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_bflag_b.out" \
	    hexdump -b "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_bflag_c.out" \
	    hexdump -b "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case c_flag
c_flag_head()
{
	atf_set "descr" "Verify -c output"
}
c_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_cflag_a.out" \
	    hexdump -c "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_cflag_b.out" \
	    hexdump -c "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_cflag_c.out" \
	    hexdump -c "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case C_flag
C_flag_head()
{
	atf_set "descr" "Verify -C output"
}
C_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_UCflag_a.out" \
	    hexdump -C "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_UCflag_b.out" \
	    hexdump -C "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_UCflag_c.out" \
	    hexdump -C "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case hd_name
hd_name_head()
{
	atf_set "descr" "Verify hd output matching -C output"
}
hd_name_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_UCflag_a.out" \
	    hd "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_UCflag_b.out" \
	    hd "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_UCflag_c.out" \
	    hd "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case d_flag
d_flag_head()
{
	atf_set "descr" "Verify -d output"
}
d_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_dflag_a.out" \
	    hexdump -d "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_dflag_b.out" \
	    hexdump -d "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_dflag_c.out" \
	    hexdump -d "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case n_flag
n_flag_head()
{
	atf_set "descr" "Check -n functionality"
}
n_flag_body()
{
	atf_check -o empty hexdump -bn 0 "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_nflag_a.out" \
	    hexdump -bn 1 "$(atf_get_srcdir)/d_hexdump_a.in"
}

atf_test_case o_flag
o_flag_head()
{
	atf_set "descr" "Verify -o output"
}
o_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_oflag_a.out" \
	    hexdump -o "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_oflag_b.out" \
	    hexdump -o "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_oflag_c.out" \
	    hexdump -o "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case s_flag
s_flag_head()
{
	atf_set "descr" "Verify -s output"
}
s_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_sflag_a.out" \
	    hexdump -bs 4 "$(atf_get_srcdir)/d_hexdump_a.in"

	atf_check -o not-empty hexdump -n 100 -s 1024 /dev/random
}

atf_test_case v_flag
v_flag_head()
{
	atf_set "descr" "Verify -v functionality"
}
v_flag_body()
{
	for i in $(seq 0 7); do
		atf_check -o match:"^\*$" \
		    hexdump -s ${i} "$(atf_get_srcdir)/d_hexdump_c.in"
		atf_check -o not-match:"^\*$" \
		    hexdump -vs ${i} "$(atf_get_srcdir)/d_hexdump_c.in"
	done

	atf_check -o not-match:"^\*$" \
	    hexdump -s 8 "$(atf_get_srcdir)/d_hexdump_c.in"
	atf_check -o not-match:"^\*$" \
	    hexdump -vs 8 "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_test_case x_flag
x_flag_head()
{
	atf_set "descr" "Verify -x output"
}
x_flag_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_xflag_a.out" \
	    hexdump -x "$(atf_get_srcdir)/d_hexdump_a.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_xflag_b.out" \
	    hexdump -x "$(atf_get_srcdir)/d_hexdump_b.in"
	atf_check -o file:"$(atf_get_srcdir)/d_hexdump_xflag_c.out" \
	    hexdump -x "$(atf_get_srcdir)/d_hexdump_c.in"
}

atf_init_test_cases()
{
	atf_add_test_case b_flag
	atf_add_test_case c_flag
	atf_add_test_case C_flag
	atf_add_test_case hd_name
	atf_add_test_case d_flag
	atf_add_test_case n_flag
	atf_add_test_case o_flag
	atf_add_test_case s_flag
	atf_add_test_case v_flag
	atf_add_test_case x_flag
}
