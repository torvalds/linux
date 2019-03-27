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

atf_test_case base16_input
base16_input_head()
{
	atf_set "descr" "Input hexadecimal numbers"
}
base16_input_body()
{
	cat > input.dc << EOF
4k	# set scale to 4 decimal places
16i	# switch to base 16
0   p
10  p
1   p
1.  p	# The '.' should have no effect
1.0 p	# Unlike with decimal, should not change the result's scale
.8  p	# Can input fractions
# Check that we can input fractions that need more scale in base 10 than in 16
# See PR 206230
.1  p
.10 p	# Result should be .0625, with scale=4
.01 p	# Result should be truncated to scale=4
8k	# Increase scale to 8 places
.01 p	# Result should be exact again
0.1 p	# Leading zeros are ignored
00.1 p	# Leading zeros are ignored
EOF
	dc input.dc > output.txt
	cat > expect.txt << EOF
0
16
1
1
1
.5
.0625
.0625
.0039
.00390625
.0625
.0625
EOF
	atf_check cmp expect.txt output.txt
}

atf_test_case base3_input
base3_input_head()
{
	atf_set "descr" "Input ternary numbers"
}
base3_input_body()
{
	cat > input.dc << EOF
4k	# 4 digits of precision
3i	# Base 3 input
0 p
1 p
10 p
.1 p	# Repeating fractions get truncated
EOF
dc input.dc > output.txt
cat > expect.txt << EOF
0
1
3
.3333
EOF
	atf_check cmp expect.txt output.txt
}

atf_init_test_cases()
{
	atf_add_test_case base16_input
	atf_add_test_case base3_input
}
