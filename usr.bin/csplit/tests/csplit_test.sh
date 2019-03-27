# Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
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

atf_test_case lines_lt_count
lines_lt_count_head()
{
	atf_set "descr" \
	    "Test an edge case where input has fewer lines than count"
}
lines_lt_count_body()
{
	cat > expectfile00 << HERE
one
two
HERE
	cat > expectfile01 << HERE
xxx 1
three
four
HERE
	cat > expectfile02 << HERE
xxx 2
five
six
HERE
	echo -e "one\ntwo\nxxx 1\nthree\nfour\nxxx 2\nfive\nsix" | \
	    csplit -k - '/xxx/' '{10}'

	atf_check cmp expectfile00 xx00
	atf_check cmp expectfile01 xx01
	atf_check cmp expectfile02 xx02
}

atf_init_test_cases()
{
	atf_add_test_case lines_lt_count
}
