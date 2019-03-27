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

# What grep(1) are we working with?
# - 0 : bsdgrep
# - 1 : gnu grep 2.51 (base)
# - 2 : gnu grep (ports)
GREP_TYPE_BSD=0
GREP_TYPE_GNU_FREEBSD=1
GREP_TYPE_GNU=2
GREP_TYPE_UNKNOWN=3

grep_type()
{
	local grep_version=$(grep --version)

	case "$grep_version" in
	*"BSD grep"*)
		return $GREP_TYPE_BSD
		;;
	*"GNU grep"*)
		case "$grep_version" in
		*2.5.1-FreeBSD*)
			return $GREP_TYPE_GNU_FREEBSD
			;;
		*)
			return $GREP_TYPE_GNU
			;;
		esac
		;;
	esac
	atf_fail "unknown grep type: $grep_version"
}

atf_test_case grep_r_implied
grep_r_implied_body()
{
	grep_type
	if [ $? -ne $GREP_TYPE_BSD ]; then
		atf_skip "this test only works with bsdgrep(1)"
	fi

	(cd "$(atf_get_srcdir)" && grep -r --exclude="*.out" -e "test" .) > d_grep_r_implied.out

	atf_check -s exit:0 -x \
	    "(cd $(atf_get_srcdir) && grep -r --exclude=\"*.out\" -e \"test\") | diff d_grep_r_implied.out -"
}

atf_test_case rgrep
rgrep_head()
{
	atf_set "require.progs" "rgrep"
}
rgrep_body()
{
	atf_check -o save:d_grep_r_implied.out grep -r --exclude="*.out" -e "test" "$(atf_get_srcdir)"
	atf_check -o file:d_grep_r_implied.out rgrep --exclude="*.out" -e "test" "$(atf_get_srcdir)"
}

atf_init_test_cases()
{
	atf_add_test_case grep_r_implied
	atf_add_test_case rgrep
}
