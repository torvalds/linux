# $NetBSD: t_sdiff.sh,v 1.1 2012/03/17 16:33:15 jruoho Exp $
# $FreeBSD$
#
# Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
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

atf_test_case flags
flags_head()
{
	atf_set "descr" "Checks -l, -s and -w flags"
}
flags_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_flags_l.out -s eq:1 \
	    sdiff -l "$(atf_get_srcdir)/d_input1" "$(atf_get_srcdir)/d_input2"

	atf_check -o file:$(atf_get_srcdir)/d_flags_s.out -s eq:1 \
	    sdiff -s "$(atf_get_srcdir)/d_input1" "$(atf_get_srcdir)/d_input2"

	atf_check -o file:$(atf_get_srcdir)/d_flags_w.out -s eq:1 \
	    sdiff -w 125 "$(atf_get_srcdir)/d_input1" "$(atf_get_srcdir)/d_input2"
}

atf_test_case iflags
iflags_head()
{
	atf_set "descr" "Checks flags -l, -s and -w combined with -I"
}
iflags_body()
{
	tail1="-w 125 -I .*filename.* $(atf_get_srcdir)/d_input1 $(atf_get_srcdir)/d_input2"
	tail2="-w 125 -I .*filename.* $(atf_get_srcdir)/d_input2 $(atf_get_srcdir)/d_input1"

	atf_check -o file:$(atf_get_srcdir)/d_iflags_a1.out -s eq:1 sdiff ${tail1}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_a2.out -s eq:1 sdiff ${tail2}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_b1.out -s eq:1 sdiff -s ${tail1}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_b2.out -s eq:1 sdiff -s ${tail2}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_c1.out -s eq:1 sdiff -l ${tail1}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_c2.out -s eq:1 sdiff -l ${tail2}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_d1.out -s eq:1 sdiff -s ${tail1}
	atf_check -o file:$(atf_get_srcdir)/d_iflags_d2.out -s eq:1 sdiff -s ${tail2}
}

atf_test_case tabs
tabs_head()
{
	atf_set "descr" "Checks comparing files containing tabs"
}
tabs_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_tabs.out -s eq:1 \
	    sdiff "$(atf_get_srcdir)/d_tabs1.in" "$(atf_get_srcdir)/d_tabs2.in"
}

atf_test_case tabends
tabends_head()
{
	atf_set "descr" "Checks correct handling of lines ended with tabs"
}
tabends_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_tabends_a.out -s eq:1 \
	    sdiff -w30 "$(atf_get_srcdir)/d_tabends.in" /dev/null

	atf_check -o file:$(atf_get_srcdir)/d_tabends_b.out -s eq:1 \
	    sdiff -w30 /dev/null "$(atf_get_srcdir)/d_tabends.in"

	atf_check -o file:$(atf_get_srcdir)/d_tabends_c.out -s eq:1 \
	    sdiff -w19 "$(atf_get_srcdir)/d_tabends.in" /dev/null
}

atf_test_case merge
merge_head()
{
	atf_set "descr" "Checks interactive merging"
}
merge_body()
{
	merge_tail="-o merge.out $(atf_get_srcdir)/d_input1 \
$(atf_get_srcdir)/d_input2 >/dev/null ; cat merge.out"

	cp $(atf_get_srcdir)/d_input* .

	atf_check -o file:d_input1 -x "yes l | sdiff ${merge_tail}"
	atf_check -o file:d_input2 -x "yes r | sdiff ${merge_tail}"

	atf_check -o file:d_input1 -x \
		"yes el | EDITOR=cat VISUAL=cat sdiff ${merge_tail}"
	atf_check -o file:d_input2 -x \
		"yes er | EDITOR=cat VISUAL=cat sdiff ${merge_tail}"

	atf_check -o file:d_input1 -x "yes l | sdiff -s ${merge_tail}"
	atf_check -o file:d_input2 -x "yes r | sdiff -s ${merge_tail}"
	atf_check -o file:d_input1 -x "yes l | sdiff -l ${merge_tail}"
	atf_check -o file:d_input2 -x "yes r | sdiff -l ${merge_tail}"
	atf_check -o file:d_input1 -x "yes l | sdiff -ls ${merge_tail}"
	atf_check -o file:d_input2 -x "yes r | sdiff -ls ${merge_tail}"

	atf_check -o file:d_input1 -x "{ while :; do echo s; echo l; \
echo v; echo l; done; } | sdiff ${merge_tail}"

	atf_check -o file:d_input2 -x "{ while :; do echo s; echo r; \
echo v; echo r; done; } | sdiff ${merge_tail}"
}

atf_test_case same
same_head()
{
	atf_set "descr" "Checks comparing file with itself"
}
same_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_same.out \
	    sdiff "$(atf_get_srcdir)/d_input1" "$(atf_get_srcdir)/d_input1"
}

atf_test_case oneline
oneline_head()
{
	atf_set "descr" "Checks comparing one-line files"
}
oneline_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_oneline_a.out -s eq:1 \
	    sdiff /dev/null "$(atf_get_srcdir)/d_oneline.in"

	atf_check -o file:$(atf_get_srcdir)/d_oneline_b.out -s eq:1 \
	    sdiff "$(atf_get_srcdir)/d_oneline.in" /dev/null
}

atf_test_case dot
dot_head()
{
	atf_set "descr" "Checks comparing with file containing only one character"
}
dot_body()
{
	echo ".                                                             <" > expout
	atf_check -o file:expout -s eq:1 sdiff "$(atf_get_srcdir)/d_dot.in" /dev/null

	echo "                                                              > ." > expout
	atf_check -o file:expout -s eq:1 sdiff /dev/null "$(atf_get_srcdir)/d_dot.in"
}

atf_test_case stdin
stdin_head()
{
	atf_set "descr" "Checks reading data from stdin"
}
stdin_body()
{
	echo "                                                              > stdin" > expout
	atf_check -o file:expout -s eq:1 -x \
	    "echo stdin | sdiff /dev/null /dev/stdin"

	echo "stdin                                                         <" > expout
	atf_check -o file:expout -s eq:1 -x \
	    "echo stdin | sdiff /dev/stdin /dev/null"
}

atf_test_case short
short_head()
{
	atf_set "descr" "Checks premature stop of merging"
}
short_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_short.out -x \
	    "printf \"r\\nl\\nr\\nl\" | sdiff -o merge.out $(atf_get_srcdir)/d_input1 \
$(atf_get_srcdir)/d_input2 >/dev/null ; cat merge.out"
}

atf_init_test_cases()
{
	atf_add_test_case flags
	atf_add_test_case iflags
	atf_add_test_case tabs
	atf_add_test_case tabends
	atf_add_test_case merge
	atf_add_test_case same
	atf_add_test_case oneline
	atf_add_test_case dot
	atf_add_test_case stdin
	atf_add_test_case short
}
