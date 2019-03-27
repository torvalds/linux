# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2016 Alan Somers
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

atf_test_case empty_r
empty_r_head()
{
	atf_set "descr" "Reverse an empty file"
}
empty_r_body()
{
	touch infile expectfile
	tail -r infile > outfile
	tail -r < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case file_r
file_r_head()
{
	atf_set "descr" "Reverse a file"
}
file_r_body()
{
	cat > infile <<HERE
This is the first line
This is the second line
This is the third line
HERE
	cat > expectfile << HERE
This is the third line
This is the second line
This is the first line
HERE
	tail -r infile > outfile
	tail -r < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case file_rn2
file_rn2_head()
{
	atf_set "descr" "Reverse the last two lines of a file"
}
file_rn2_body()
{
	cat > infile <<HERE
This is the first line
This is the second line
This is the third line
HERE
	cat > expectfile << HERE
This is the third line
This is the second line
HERE
	tail -rn2 infile > outfile
	tail -rn2 < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

# Regression test for PR 222671
# https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=222671
atf_test_case pipe_leading_newline_r
pipe_leading_newline_r_head()
{
	atf_set "descr" "Reverse a pipe whose first character is a newline"
}
pipe_leading_newline_r_body()
{
	cat > expectfile << HERE
3
2
1

HERE
	printf '\n1\n2\n3\n' | tail -r > outfile
	printf '\n1\n2\n3\n' | tail -r > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case file_rc28
file_rc28_head()
{
	atf_set "descr" "Reverse a file and display the last 28 characters"
}
file_rc28_body()
{
	cat > infile <<HERE
This is the first line
This is the second line
This is the third line
HERE
	cat > expectfile << HERE
This is the third line
line
HERE
	tail -rc28 infile > outfile
	tail -rc28 < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case file_rc28
file_rc28_head()
{
	atf_set "descr" "Reverse a file and display the last 28 characters"
}
file_rc28_body()
{
	cat > infile <<HERE
This is the first line
This is the second line
This is the third line
HERE
	cat > expectfile << HERE
This is the third line
line
HERE
	tail -rc28 infile > outfile
	tail -rc28 < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case longfile_r
longfile_r_head()
{
	atf_set "descr" "Reverse a long file"
}
longfile_r_body()
{
	jot -w "%0511d" 1030 0 > infile
	jot -w "%0511d" 1030 1029 0 -1 > expectfile
	tail -r infile > outfile
	tail -r < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case longfile_r_enomem
longfile_r_enomem_head()
{
	atf_set "descr" "Reverse a file that's too long to store in RAM"
}
longfile_r_enomem_body()
{
	# When we reverse a file that's too long for RAM, tail should drop the
	# first part and just print what it can.  We'll check that the last
	# part is ok
	{
		ulimit -v 32768 || atf_skip "Can't adjust ulimit"
		jot -w "%01023d" 32768 0 | tail -r > outfile ;
	}
	if [ "$?" -ne 1 ]; then
		atf_skip "Didn't get ENOMEM.  Adjust test parameters"
	fi
	# We don't know how much of the input we dropped.  So just check that
	# the first ten lines of tail's output are the same as the last ten of
	# the input
	jot -w "%01023d" 10 32767 0 -1 > expectfile
	head -n 10 outfile > outtrunc
	diff expectfile outtrunc
	atf_check cmp expectfile outtrunc
}

atf_test_case longfile_r_longlines
longfile_r_longlines_head()
{
	atf_set "descr" "Reverse a long file with extremely long lines"
}
longfile_r_longlines_body()
{
	jot -s " " -w "%07d" 18000 0 > infile
	jot -s " " -w "%07d" 18000 18000 >> infile
	jot -s " " -w "%07d" 18000 36000 >> infile
	jot -s " " -w "%07d" 18000 36000 > expectfile
	jot -s " " -w "%07d" 18000 18000 >> expectfile
	jot -s " " -w "%07d" 18000 0 >> expectfile
	tail -r infile > outfile
	tail -r < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case longfile_rc135782
longfile_rc135782_head()
{
	atf_set "descr" "Reverse a long file and print the last 135,782 bytes"
}
longfile_rc135782_body()
{
	jot -w "%063d" 9000 0 > infile
	jot -w "%063d" 2121 8999 0 -1 > expectfile
	echo "0000000000000000000000000000000006878" >> expectfile
	tail -rc135782 infile > outfile
	tail -rc135782 < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case longfile_rc145782_longlines
longfile_rc145782_longlines_head()
{
	atf_set "descr" "Reverse a long file with extremely long lines and print the last 145,782 bytes"
}
longfile_rc145782_longlines_body()
{
	jot -s " " -w "%07d" 18000 0 > infile
	jot -s " " -w "%07d" 18000 18000 >> infile
	jot -s " " -w "%07d" 18000 36000 >> infile
	jot -s " " -w "%07d" 18000 36000 > expectfile
	echo -n "35777 " >> expectfile
	jot -s " " -w "%07d" 222 35778 >> expectfile
	tail -rc145782 infile > outfile
	tail -rc145782 < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case longfile_rn2500
longfile_rn2500_head()
{
	atf_set "descr" "Reverse a long file and print the last 2,500 lines"
}
longfile_rn2500_body()
{
	jot -w "%063d" 9000 0 > infile
	jot -w "%063d" 2500 8999 0 -1 > expectfile
	tail -rn2500 infile > outfile
	tail -rn2500 < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case broken_pipe
broken_pipe_head()
{
	atf_set "descr" "Do not print bogus errno based output on short writes"
}
broken_pipe_body()
{
	atf_check -o save:ints seq -f '%128g' 1 1000
	atf_check -s ignore \
	    -e "inline:tail: stdout\nexit code: 1\n" \
	    -x '(tail -n 856 ints; echo exit code: $? >&2) | sleep 2'
}


atf_init_test_cases()
{
	atf_add_test_case empty_r
	atf_add_test_case file_r
	atf_add_test_case file_rc28
	atf_add_test_case file_rn2
	atf_add_test_case pipe_leading_newline_r
	# The longfile tests are designed to exercise behavior in r_buf(),
	# which operates on 128KB blocks
	atf_add_test_case longfile_r
	atf_add_test_case longfile_r_enomem
	atf_add_test_case longfile_r_longlines
	atf_add_test_case longfile_rc135782
	atf_add_test_case longfile_rc145782_longlines
	atf_add_test_case longfile_rn2500
	atf_add_test_case broken_pipe
}
