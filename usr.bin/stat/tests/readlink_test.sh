#
# Copyright (c) 2017 Dell EMC
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

atf_test_case f_flag
basic_head()
{
	atf_set	"descr" "Verify that calling readlink without any flags " \
			"prints out the symlink target for a file"
}
basic_body()
{
	atf_check ln -s foo bar
	atf_check -o inline:'foo\n' readlink bar
}

atf_test_case f_flag
f_flag_head()
{
	atf_set	"descr" "Verify that calling readlink with -f will not emit " \
			"an error message/exit with a non-zero code"
}
f_flag_body()
{
	atf_check touch A.file
	atf_check ln -s nonexistent A.link
	atf_check -o inline:"nonexistent\n" \
	    -s exit:1 readlink A.file A.link
	atf_check -o inline:"$(realpath A.file)\n$PWD/nonexistent\n" \
	    -s exit:1 readlink -f A.file A.link
}

atf_test_case n_flag
n_flag_head()
{
}
n_flag_body()
{
	atf_check ln -s nonexistent.A A
	atf_check ln -s nonexistent.B B
	atf_check -o 'inline:nonexistent.A\nnonexistent.B\n' readlink A B
	atf_check -o 'inline:nonexistent.Anonexistent.B' readlink -n A B
}

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case f_flag
	atf_add_test_case n_flag
}
