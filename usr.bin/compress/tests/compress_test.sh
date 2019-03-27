# Copyright (c) 2017 Jilles Tjoelker <jilles@FreeBSD.org>
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

atf_test_case uncompress_file_1
uncompress_file_1_head()
{
	atf_set "descr" \
	    "Test uncompress without options, filename with .Z"
}
uncompress_file_1_body()
{
	printf '%01000d\n' 7 >expectfile1
	printf "\
\037\235\220\060\002\012\034\110\260\240\301\203\010\023\052\134\
\310\260\241\303\207\020\043\112\234\110\261\242\305\213\030\063\
\152\334\310\261\243\307\217\040\103\212\034\111\262\244\311\223\
\050\123\252\134\211\360\206\002" >file1.Z
	atf_check uncompress file1.Z
	atf_check cmp file1 expectfile1
	atf_check test ! -e file1.Z
}

atf_test_case uncompress_file_2
uncompress_file_2_head()
{
	atf_set "descr" \
	    "Test uncompress without options, filename without .Z"
}
uncompress_file_2_body()
{
	printf '%01000d\n' 7 >expectfile1
	printf "\
\037\235\220\060\002\012\034\110\260\240\301\203\010\023\052\134\
\310\260\241\303\207\020\043\112\234\110\261\242\305\213\030\063\
\152\334\310\261\243\307\217\040\103\212\034\111\262\244\311\223\
\050\123\252\134\211\360\206\002" >file1.Z
	atf_check uncompress file1
	atf_check cmp file1 expectfile1
	atf_check test ! -e file1.Z
}

atf_test_case uncompress_stdio_1
uncompress_stdio_1_head()
{
	atf_set "descr" \
	    "Test uncompress without parameters"
}
uncompress_stdio_1_body()
{
	printf '%01000d\n' 7 >expectfile1
	printf "\
\037\235\220\060\002\012\034\110\260\240\301\203\010\023\052\134\
\310\260\241\303\207\020\043\112\234\110\261\242\305\213\030\063\
\152\334\310\261\243\307\217\040\103\212\034\111\262\244\311\223\
\050\123\252\134\211\360\206\002" >file1.Z
	atf_check -o file:expectfile1 -x 'uncompress <file1.Z'
}

atf_test_case uncompress_minusc_1
uncompress_minusc_1_head()
{
	atf_set "descr" \
	    "Test uncompress with -c"
}
uncompress_minusc_1_body()
{
	printf '%01000d\n' 7 >expectfile1
	printf "\
\037\235\220\060\002\012\034\110\260\240\301\203\010\023\052\134\
\310\260\241\303\207\020\043\112\234\110\261\242\305\213\030\063\
\152\334\310\261\243\307\217\040\103\212\034\111\262\244\311\223\
\050\123\252\134\211\360\206\002" >file1.Z
	atf_check -o file:expectfile1 uncompress -c file1.Z
	atf_check test -e file1.Z
	atf_check test ! -e file1
}

atf_test_case compress_uncompress_stdio_1
compress_uncompress_stdio_1_head()
{
	atf_set "descr" \
	    "Test compressing and uncompressing some data, using stdio"
}
compress_uncompress_stdio_1_body()
{
	printf '%01000d\n' 7 8 >expectfile1
	atf_check -x 'compress <expectfile1 >file1.Z'
	atf_check -o file:expectfile1 uncompress -c file1.Z
}

atf_test_case compress_uncompress_minusc_1
compress_uncompress_minusc_1_head()
{
	atf_set "descr" \
	    "Test compressing and uncompressing some data, using -c"
}
compress_uncompress_minusc_1_body()
{
	printf '%01000d\n' 7 8 >expectfile1
	atf_check -x 'compress -c expectfile1 >file1.Z'
	atf_check -o file:expectfile1 uncompress -c file1.Z
}

atf_test_case compress_uncompress_file_1
compress_uncompress_file_1_head()
{
	atf_set "descr" \
	    "Test compressing and uncompressing some data, passing one filename"
}
compress_uncompress_file_1_body()
{
	printf '%01000d\n' 7 8 >expectfile1
	cp expectfile1 file1
	atf_check compress file1
	atf_check -s exit:1 cmp -s file1.Z expectfile1
	atf_check uncompress file1.Z
	atf_check cmp file1 expectfile1
}

atf_test_case compress_uncompress_file_2
compress_uncompress_file_2_head()
{
	atf_set "descr" \
	    "Test compressing and uncompressing some data, passing two filenames"
}
compress_uncompress_file_2_body()
{
	printf '%01000d\n' 7 8 >expectfile1
	printf '%01000d\n' 8 7 >expectfile2
	cp expectfile1 file1
	cp expectfile2 file2
	atf_check compress file1 file2
	atf_check -s exit:1 cmp -s file1.Z expectfile1
	atf_check -s exit:1 cmp -s file2.Z expectfile2
	atf_check -s exit:1 cmp -s file1.Z file2.Z
	atf_check uncompress file1.Z file2.Z
	atf_check cmp file1 expectfile1
	atf_check cmp file2 expectfile2
}

atf_test_case compress_uncompress_file_minusc_1
compress_uncompress_file_minusc_1_head()
{
	atf_set "descr" \
	    "Test compressing and uncompressing some data, passing two filenames to uncompress -c"
}
compress_uncompress_file_minusc_1_body()
{
	printf '%01000d\n' 7 8 >expectfile1
	printf '%01000d\n' 8 7 >expectfile2
	cp expectfile1 file1
	cp expectfile2 file2
	atf_check compress file1 file2
	atf_check -s exit:1 cmp -s file1.Z expectfile1
	atf_check -s exit:1 cmp -s file2.Z expectfile2
	atf_check -s exit:1 cmp -s file1.Z file2.Z
	atf_check -x 'uncompress -c file1.Z file2.Z >all'
	atf_check -x 'cat expectfile1 expectfile2 >expectall'
	atf_check cmp all expectall
}

atf_init_test_cases()
{
	atf_add_test_case uncompress_file_1
	atf_add_test_case uncompress_file_2
	atf_add_test_case uncompress_stdio_1
	atf_add_test_case uncompress_minusc_1
	atf_add_test_case compress_uncompress_stdio_1
	atf_add_test_case compress_uncompress_minusc_1
	atf_add_test_case compress_uncompress_file_1
	atf_add_test_case compress_uncompress_file_2
	atf_add_test_case compress_uncompress_file_minusc_1
}
