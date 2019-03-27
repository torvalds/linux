#
# Copyright (c) 2017 Kyle Evans <kevans@FreeBSD.org>
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

check()
{
	local dataname="${1}"; shift

	if [ "${dataname}" == "gnuext" ]; then
		atf_expect_fail "GNU extensions are not currently implemented"
	fi

	prog="$(atf_get_srcdir)/h_regex"
	data="$(atf_get_srcdir)/data/${dataname}.in"

	atf_check -x "${prog} <${data}"
	atf_check -x "${prog} -el <${data}"
	atf_check -x "${prog} -er <${data}"
}

create_tc()
{
	local name="${1}"; shift
	local descr="${1}"; shift

	atf_test_case "${name}"
	eval "${name}_head() { atf_set 'descr' '${descr}'; }"
	eval "${name}_body() { check '${name}'; }"

	atf_add_test_case "${name}"
}

atf_init_test_cases()
{
	create_tc gnuext "Check GNU extension functionality"
}
