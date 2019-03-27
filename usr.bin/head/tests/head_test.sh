# Copyright (c) 2017 Fred Schlechter 
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

atf_test_case empty_file 
empty_file_head() {
	atf_set "descr" "Test head(1)'s handling of an empty file"
}
empty_file_body() {
	touch infile expectfile
	head infile > outfile
	head < infile > outpipe
	atf_check cmp expectfile outfile
	atf_check cmp expectfile outpipe
}

atf_test_case default_no_options
default_no_options_head() {
	atf_set "descr" "Test head(1)'s default mode"
}
default_no_options_body() {
	#head(1) is supposed to default to 10 lines of output. Verify that it does that.
	jot -b test 10 > expectfile
	jot -b test 100 > infile
	head infile > outfile
	atf_check -e empty cmp expectfile outfile
}

atf_test_case line_count
line_count_head() {
	atf_set "descr" "Test head(1)'s -n option" 
}
line_count_body() {
	jot -b test 100 > outfile
	head -n 50 outfile > expectfile
	atf_check -o inline:"      50 expectfile\n" wc -l expectfile
}

atf_test_case byte_count
byte_count_head() {
	atf_set "descr" "Test head(1)'s -c option"
}
byte_count_body() {
	jot -b test 100 > outfile
	head -c 50 outfile > expectfile
	atf_check -o inline:"      50 expectfile\n" wc -c expectfile
}

atf_test_case sparse_file_text_at_beginning
sparse_file_text_at_beginning_head() {
	atf_set "descr" "Test head(1)'s handling of a sparse file with text at the beginning of the file"
}
sparse_file_text_at_beginning_body () {
	jot -b test 10 > outfile
	truncate -s +1K outfile
	head -c 512 outfile > expectfile
	atf_check -o inline:"     512 expectfile\n" wc -c expectfile
}

atf_test_case sparse_file_text_at_end
sparse_file_text_at_end_head() {
	atf_set "descr" "Test head(1)'s handling of a sparse file with text at the end of the file"
}
sparse_file_text_at_end_body () {
	truncate -s +1K infile 
	echo test >> infile 
	head -c 4096 < infile > outpipe
	atf_check cmp infile outpipe 
}

atf_test_case missing_line_count
missing_line_count_head() {
	atf_set "descr" "Test head(1)'s handling of a missing line count arg"
}
missing_line_count_body () {
	jot -b test 100 > outfile
	atf_check -s not-exit:0 -e not-empty head -n outfile 
}

atf_test_case invalid_line_count
invalid_line_count_head() {
	atf_set "descr" "Test head(1)'s handling of an invalid line count arg"
}
invalid_line_count_body () {
	jot -b test 100 > outfile
	atf_check -s not-exit:0 -e not-empty head -n -10 outfile 
}

atf_test_case read_from_stdin
read_from_stdin_head() {
	atf_set "descr" "Test head(1)'s reading of stdin"
}
read_from_stdin_body() {
	#head(1) defaults to head -n 10 if no args are given.
	jot -b test 10 > outfile
	jot -b test 20 | head > expectfile
	atf_check cmp outfile expectfile 
}

atf_init_test_cases() {
	atf_add_test_case empty_file 
	atf_add_test_case default_no_options
	atf_add_test_case line_count
	atf_add_test_case byte_count
	atf_add_test_case sparse_file_text_at_beginning
	atf_add_test_case sparse_file_text_at_end
	atf_add_test_case missing_line_count
	atf_add_test_case invalid_line_count
	atf_add_test_case read_from_stdin
}
