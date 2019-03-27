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

atf_test_case bmod
bmod_head()
{
	atf_set "descr" "Tests the remainder % operator"
}
bmod_body()
{
	cat > input.dc << EOF
0 3 % p		# basic usage
1 3 % p
2 3 % p
3 3 % p
4 3 % p
_1 3 % p	# negative dividends work like a remainder, not a modulo
1 _3 % p	# negative divisors use the divisor's absolute value
1k		# fractional remainders
5 3 % p
6 5 % p
5.4 3 % p
_.1 3 % p
1.1 _3 % p
1 .3 % p
EOF
	dc input.dc > output.txt
	cat > expect.txt << EOF
0
1
2
0
1
-1
1
2
1
2.4
-.1
1.1
.1
EOF
	atf_check cmp expect.txt output.txt
}

atf_test_case bmod_by_zero
bmod_by_zero_head()
{
	atf_set "descr" "remaindering by zero should print a warning"
}
bmod_by_zero_body()
{
	atf_check -e match:"remainder by zero" dc -e '1 0 %'
}

atf_test_case bdivmod
bdivmod_head()
{
	atf_set "descr" "Tests the divide and modulo ~ operator"
}
bdivmod_body()
{
	cat > input.dc << EOF
0 3 ~ n32Pp	# basic usage
1 3 ~ n32Pp
2 3 ~ n32Pp
3 3 ~ n32Pp
4 3 ~ n32Pp
_1 3 ~ n32Pp	# negative dividends work like a remainder, not a modulo
_4 3 ~ n32Pp	# sign of quotient and divisor must agree
1 _3 ~ n32Pp	# negative divisors use the divisor's absolute value
1k		# fractional remainders
5 3 ~ n32Pp
6 5 ~ n32Pp
5.4 3 ~ n32Pp
_.1 3 ~ n32Pp
1.1 _3 ~ n32Pp
1 .3 ~ n32Pp
4k
.01 .003 ~ n32Pp	# divmod quotient always has scale=0
EOF
	dc input.dc > output.txt
	cat > expect.txt << EOF
0 0
1 0
2 0
0 1
1 1
-1 0
-1 -1
1 0
2 1.6
1 1.2
2.4 1.8
-.1 0.0
1.1 -.3
.1 3.3
.001 3.3333
EOF
	atf_check cmp expect.txt output.txt
}

atf_test_case bdivmod_by_zero
bdivmod_by_zero_head()
{
	atf_set "descr" "divmodding by zero should print a warning"
}
bdivmod_by_zero_body()
{
	atf_check -e match:"divide by zero" dc -e '1 0 ~'
}

atf_init_test_cases()
{
	atf_add_test_case bmod
	atf_add_test_case bmod_by_zero
	atf_add_test_case bdivmod
	atf_add_test_case bdivmod_by_zero
}
