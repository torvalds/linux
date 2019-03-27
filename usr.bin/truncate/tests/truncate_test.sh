#
# Copyright 2014, Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * Neither the name of Google Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#

# Helper function that is always used to create and fill stderr.txt for these
# tests.
_custom_create_file()
{
	# The first argument is a command.
	# The second is just a string.
	case "${1}" in
		creat) > stderr.txt ;;
		print) [ "${2}" ] && \
		    printf "%s\n" "${2}" >> stderr.txt ;;
	esac
}

# Helper function that create the file stderr.txt that contains the string
# passed in as the first argument.
create_stderr_file()
{
	_custom_create_file creat
	_custom_create_file print "${1}"
}

# Helper function that create the file stderr.txt that contains the expected
# truncate utility usage message.
create_stderr_usage_file()
{
	_custom_create_file creat
	_custom_create_file print "${1}"
	_custom_create_file print \
	    "usage: truncate [-c] -s [+|-|%|/]size[K|k|M|m|G|g|T|t] file ..."
	_custom_create_file print "       truncate [-c] -r rfile file ..."
}

atf_test_case illegal_option
illegal_option_head()
{
	atf_set "descr" "Verifies that truncate exits >0 when passed an" \
	    "invalid command line option"
}
illegal_option_body()
{
	create_stderr_usage_file 'truncate: illegal option -- 7'

	# We expect the error message, with no new files.
	atf_check -s not-exit:0 -e file:stderr.txt truncate -7 -s0 output.txt
	[ ! -e output.txt ] || atf_fail "output.txt should not exist"
}

atf_test_case illegal_size
illegal_size_head()
{
	atf_set "descr" "Verifies that truncate exits >0 when passed an" \
	    "invalid power of two convention"
}
illegal_size_body()
{
	create_stderr_file "truncate: invalid size argument \`+1L'"

	# We expect the error message, with no new files.
	atf_check -s not-exit:0 -e file:stderr.txt truncate -s+1L output.txt
	[ ! -e output.txt ] || atf_fail "output.txt should not exist"
}

atf_test_case too_large_size
too_large_size_head()
{
	atf_set "descr" "Verifies that truncate exits >0 when passed an" \
	    "a size that is INT64_MAX < size <= UINT64_MAX"
}
too_large_size_body()
{
	create_stderr_file "truncate: invalid size argument \`8388608t'"

	# We expect the error message, with no new files.
	atf_check -s not-exit:0 -e file:stderr.txt \
	    truncate -s8388608t output.txt
	[ ! -e output.txt ] || atf_fail "output.txt should not exist"
}

atf_test_case opt_c
opt_c_head()
{
	atf_set "descr" "Verifies that -c prevents creation of new files"
}
opt_c_body()
{
	# No new files and truncate returns 0 as if this is a success.
	atf_check truncate -c -s 0 doesnotexist.txt
	[ ! -e output.txt ] || atf_fail "doesnotexist.txt should not exist"
	> reference
	atf_check truncate -c -r reference doesnotexist.txt
	[ ! -e output.txt ] || atf_fail "doesnotexist.txt should not exist"

	create_stderr_file

	# The existing file will be altered by truncate.
	> exists.txt
	atf_check -e file:stderr.txt truncate -c -s1 exists.txt
	[ -s exists.txt ] || atf_fail "exists.txt be larger than zero bytes"
}

atf_test_case opt_rs
opt_rs_head()
{
	atf_set "descr" "Verifies that truncate command line flags" \
	    "-s and -r cannot be specifed together"
}
opt_rs_body()
{
	create_stderr_usage_file

	# Force an error due to the use of both -s and -r.
	> afile
	atf_check -s not-exit:0 -e file:stderr.txt truncate -s0 -r afile afile
}

atf_test_case no_files
no_files_head()
{
	atf_set "descr" "Verifies that truncate needs a list of files on" \
	    "the command line"
}
no_files_body()
{
	create_stderr_usage_file

	# A list of files must be present on the command line.
	atf_check -s not-exit:0 -e file:stderr.txt truncate -s1
}

atf_test_case bad_refer
bad_refer_head()
{
	atf_set "descr" "Verifies that truncate detects a non-existent" \
	    "reference file"
}
bad_refer_body()
{
	create_stderr_file "truncate: afile: No such file or directory"

	# The reference file must exist before you try to use it.
	atf_check -s not-exit:0 -e file:stderr.txt truncate -r afile afile
	[ ! -e afile ] || atf_fail "afile should not exist"
}

atf_test_case bad_truncate
bad_truncate_head()
{
	atf_set "descr" "Verifies that truncate reports an error during" \
	    "truncation"
	atf_set "require.user" "unprivileged"
}
bad_truncate_body()
{
	create_stderr_file "truncate: exists.txt: Permission denied"

	# Trying to get the ftruncate() call to return -1.
	> exists.txt
	atf_check chmod 444 exists.txt

	atf_check -s not-exit:0 -e file:stderr.txt truncate -s1 exists.txt
}

atf_test_case new_absolute_grow
new_absolute_grow_head()
{
	atf_set "descr" "Verifies truncate can make and grow a new 1m file"
}
new_absolute_grow_body()
{
	create_stderr_file

	# Create a new file and grow it to 1024 bytes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s1k output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1024 ] || atf_fail "expected file size of 1k"

	create_stderr_file

	# Grow the existing file to 1M.  We are using absolute sizes.
	atf_check -s exit:0 -e file:stderr.txt truncate -c -s1M output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1048576 ] || atf_fail "expected file size of 1m"
}

atf_test_case new_absolute_shrink
new_absolute_shrink_head()
{
	atf_set "descr" "Verifies that truncate can make and" \
	    "shrink a new 1m file"
}
new_absolute_shrink_body()
{
	create_stderr_file

	# Create a new file and grow it to 1048576 bytes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s1M output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1048576 ] || atf_fail "expected file size of 1m"

	create_stderr_file

	# Shrink the existing file to 1k.  We are using absolute sizes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s1k output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1024 ] || atf_fail "expected file size of 1k"
}

atf_test_case new_relative_grow
new_relative_grow_head()
{
	atf_set "descr" "Verifies truncate can make and grow a new 1m file" \
	    "using relative sizes"
}
new_relative_grow_body()
{
	create_stderr_file

	# Create a new file and grow it to 1024 bytes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s+1k output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1024 ] || atf_fail "expected file size of 1k"

	create_stderr_file

	# Grow the existing file to 1M.  We are using relative sizes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s+1047552 output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1048576 ] || atf_fail "expected file size of 1m"
}

atf_test_case new_relative_shrink
new_relative_shrink_head()
{
	atf_set "descr" "Verifies truncate can make and shrink a new 1m file" \
	    "using relative sizes"
}
new_relative_shrink_body()
{
	create_stderr_file

	# Create a new file and grow it to 1049600 bytes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s+1049600 output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1049600 ] || atf_fail "expected file size of 1m"

	create_stderr_file

	# Shrink the existing file to 1k.  We are using relative sizes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s-1M output.txt
	atf_check -s exit:1 cmp -s output.txt /dev/zero
	eval $(stat -s output.txt)
	[ ${st_size} -eq 1024 ] || atf_fail "expected file size of 1k"
}

atf_test_case cannot_open
cannot_open_head()
{
	atf_set "descr" "Verifies truncate handles open failures correctly" \
	    "in a list of files"
	atf_set "require.user" "unprivileged"
}
cannot_open_body()
{
	# Create three files -- the middle file cannot allow writes.
	> before
	> 0000
	> after
	atf_check chmod 0000 0000

	create_stderr_file "truncate: 0000: Permission denied"

	# Create a new file and grow it to 1024 bytes.
	atf_check -s not-exit:0 -e file:stderr.txt \
	truncate -c -s1k before 0000 after
	eval $(stat -s before)
	[ ${st_size} -eq 1024 ] || atf_fail "expected file size of 1k"
	eval $(stat -s after)
	[ ${st_size} -eq 1024 ] || atf_fail "expected file size of 1k"
	eval $(stat -s 0000)
	[ ${st_size} -eq 0 ] || atf_fail "expected file size of zero"
}

atf_test_case reference
reference_head()
{
	atf_set "descr" "Verifies that truncate can use a reference file"
}
reference_body()
{
	# Create a 4 byte reference file.
	printf "123\n" > reference
	eval $(stat -s reference)
	[ ${st_size} -eq 4 ] || atf_fail "reference file should be 4 bytes"

	create_stderr_file

	# Create a new file and grow it to 4 bytes.
	atf_check -e file:stderr.txt truncate -r reference afile
	eval $(stat -s afile)
	[ ${st_size} -eq 4 ] || atf_fail "new file should also be 4 bytes"
}

atf_test_case new_zero
new_zero_head()
{
	atf_set "descr" "Verifies truncate can make and grow zero byte file"
}
new_zero_body()
{
	create_stderr_file

	# Create a new file and grow it to zero bytes.
	atf_check -s exit:0 -e file:stderr.txt truncate -s0 output.txt
	eval $(stat -s output.txt)
	[ ${st_size} -eq 0 ] || atf_fail "expected file size of zero"

	# Pretend to grow the file.
	atf_check -s exit:0 -e file:stderr.txt truncate -s+0 output.txt
	eval $(stat -s output.txt)
	[ ${st_size} -eq 0 ] || atf_fail "expected file size of zero"
}

atf_test_case negative
negative_head()
{
	atf_set "descr" "Verifies truncate treats negative sizes as zero"
}
negative_body()
{
	# Create a 5 byte file.
	printf "abcd\n" > afile
	eval $(stat -s afile)
	[ ${st_size} -eq 5 ] || atf_fail "afile file should be 5 bytes"

	create_stderr_file

	# Create a new file and do a 100 byte negative relative shrink.
	atf_check -e file:stderr.txt truncate -s-100 afile
	eval $(stat -s afile)
	[ ${st_size} -eq 0 ] || atf_fail "new file should now be zero bytes"
}

atf_test_case roundup
roundup_head()
{
	atf_set "descr" "Verifies truncate round up"
}
roundup_body()
{
	# Create a 5 byte file.
	printf "abcd\n" > afile
	eval $(stat -s afile)
	[ ${st_size} -eq 5 ] || atf_fail "afile file should be 5 bytes"

	create_stderr_file

	# Create a new file and do a 100 byte roundup.
	atf_check -e file:stderr.txt truncate -s%100 afile
	eval $(stat -s afile)
	[ ${st_size} -eq 100 ] || atf_fail "new file should now be 100 bytes"
}

atf_test_case rounddown
rounddown_head()
{
	atf_set "descr" "Verifies truncate round down"
}
rounddown_body()
{
	# Create a 5 byte file.
	printf "abcd\n" > afile
	eval $(stat -s afile)
	[ ${st_size} -eq 5 ] || atf_fail "afile file should be 5 bytes"

	create_stderr_file

	# Create a new file and do a 2 byte roundup.
	atf_check -e file:stderr.txt truncate -s/2 afile
	eval $(stat -s afile)
	[ ${st_size} -eq 4 ] || atf_fail "new file should now be 4 bytes"
}

atf_test_case rounddown_zero
rounddown_zero_head()
{
	atf_set "descr" "Verifies truncate round down to zero"
}
rounddown_zero_body()
{
	# Create a 5 byte file.
	printf "abcd\n" > afile
	eval $(stat -s afile)
	[ ${st_size} -eq 5 ] || atf_fail "afile file should be 5 bytes"

	create_stderr_file

	# Create a new file and do a 10 byte roundup.
	atf_check -e file:stderr.txt truncate -s/10 afile
	eval $(stat -s afile)
	[ ${st_size} -eq 0 ] || atf_fail "new file should now be 0 bytes"
}

atf_init_test_cases()
{
	atf_add_test_case illegal_option
	atf_add_test_case illegal_size
	atf_add_test_case too_large_size
	atf_add_test_case opt_c
	atf_add_test_case opt_rs
	atf_add_test_case no_files
	atf_add_test_case bad_refer
	atf_add_test_case bad_truncate
	atf_add_test_case cannot_open
	atf_add_test_case new_absolute_grow
	atf_add_test_case new_absolute_shrink
	atf_add_test_case new_relative_grow
	atf_add_test_case new_relative_shrink
	atf_add_test_case reference
	atf_add_test_case new_zero
	atf_add_test_case negative
	atf_add_test_case roundup
	atf_add_test_case rounddown
	atf_add_test_case rounddown_zero
}
