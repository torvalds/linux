# Copyright (c) 2019 Conrad Meyer <cem@FreeBSD.org>
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

atf_test_case float_rounding
float_rounding_head()
{
	atf_set "descr" "Check for correct termination in the face of floating point rounding"
}
float_rounding_body()
{
	atf_check -o inline:'1\n1.1\n1.2\n' seq 1 0.1 1.2
}

atf_test_case format_includes_conversion
format_includes_conversion_head()
{
	atf_set "descr" "Check for correct user-provided format strings"
}
format_includes_conversion_body()
{
	# PR 236347
	atf_check -s exit:1 -o empty -e match:"invalid format string" \
	    seq -f foo 3
	atf_check -s exit:0 -o inline:'foo1\nfoo2\n' -e empty \
	    seq -f foo%g 2
}

atf_init_test_cases()
{
	atf_add_test_case float_rounding
	atf_add_test_case format_includes_conversion
}
