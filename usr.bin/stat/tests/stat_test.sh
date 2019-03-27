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

atf_test_case F_flag
F_flag_head()
{
	atf_set	"descr" "Verify the output format for -F"
}
F_flag_body()
{
	# TODO: socket, whiteout file
	atf_check touch a
	atf_check mkdir b
	atf_check install -m 0777 /dev/null c
	atf_check ln -s a d
	atf_check mkfifo f

	atf_check -o match:'.* a' stat -Fn a
	atf_check -o match:'.* b/' stat -Fn b
	atf_check -o match:'.* c\*' stat -Fn c
	atf_check -o match:'.* d@' stat -Fn d
	atf_check -o match:'.* f\|' stat -Fn f
}

atf_test_case l_flag
l_flag_head()
{
	atf_set	"descr" "Verify the output format for -l"
}
l_flag_body()
{
	atf_check touch a
	atf_check ln a b
	atf_check ln -s a c
	atf_check mkdir d

	paths="a b c d"

	ls_out=ls.output
	stat_out=stat.output

	# NOTE:
	# - Even though stat -l claims to be equivalent to `ls -lT`, the
	#   whitespace is a bit more liberal in the `ls -lT` output.
	# - `ls -ldT` is used to not recursively list the contents of
	#   directories.
	for path in $paths; do
		atf_check -o save:$ls_out ls -ldT $path
		cat $ls_out
		atf_check -o save:$stat_out stat -l $path
		cat $stat_out
		echo "Comparing normalized whitespace"
		atf_check sed -i '' -E -e 's/[[:space:]]+/ /g' $ls_out
		atf_check sed -i '' -E -e 's/[[:space:]]+/ /g' $stat_out
		atf_check cmp $ls_out $stat_out
	done
}

atf_test_case n_flag
n_flag_head()
{
	atf_set	"descr" "Verify that -n suppresses newline output for lines"
}
n_flag_body()
{
	atf_check touch a b
	atf_check -o inline:"$(stat a | tr -d '\n')" stat -n a
	atf_check -o inline:"$(stat a b | tr -d '\n')" stat -n a b
}

atf_test_case q_flag
q_flag_head()
{
	atf_set	"descr" "Verify that -q suppresses error messages from l?stat(2)"
}
q_flag_body()
{
	ln -s nonexistent broken-link

	atf_check -s exit:1 stat -q nonexistent
	atf_check -s exit:1 stat -q nonexistent
	atf_check -o not-empty stat -q broken-link
	atf_check -o not-empty stat -qL broken-link
}

atf_test_case r_flag
r_flag_head()
{
	atf_set	"descr" "Verify that -r displays output in 'raw mode'"
}
r_flag_body()
{
	atf_check touch a
	# TODO: add more thorough checks.
	atf_check -o not-empty stat -r a
}

atf_test_case s_flag
s_flag_head()
{
	atf_set	"descr" "Verify the output format for -s"
}
s_flag_body()
{
	atf_check touch a
	atf_check ln a b
	atf_check ln -s a c
	atf_check mkdir d

	paths="a b c d"

	# The order/name of each of the fields is specified by stat(1) manpage.
	fields="st_dev st_ino st_mode st_nlink"
	fields="$fields st_uid st_gid st_rdev st_size"
	fields="$fields st_uid st_gid st_mode"
	fields="$fields st_atime st_mtime st_ctime st_birthtime"
	fields="$fields st_blksize st_blocks st_flags"

	# NOTE: the following...
	# - ... relies on set -eu to ensure that the fields are set, as
	#       documented, in stat(1).
	# - ... uses a subshell to ensure that the eval'ed variables don't
	#	pollute the next iteration's behavior.
	for path in $paths; do
		(
		set -eu
		eval $(stat -s $path)
		for field in $fields; do
			eval "$field=\$$field"
		done
		) || atf_fail 'One or more fields not set by stat(1)'
	done
}

atf_test_case t_flag
t_flag_head()
{
	atf_set	"descr" "Verify the output format for -t"
}

t_flag_body()
{
	atf_check touch foo
	atf_check touch -d 1970-01-01T00:00:42 foo
	atf_check -o inline:'42\n' \
	    stat -t '%s' -f '%a' foo
	atf_check -o inline:'1970-01-01 00:00:42\n' \
	    stat -t '%F %H:%M:%S' -f '%Sa' foo
}

x_output_date()
{
	local date_format='%a %b %e %H:%M:%S %Y'

	stat -t "$date_format" "$@"
}

x_output()
{
	local path=$1; shift

	local atime_s=$(x_output_date -f '%Sa' $path)
	local ctime_s=$(x_output_date -f '%Sc' $path)
	local devid=$(stat -f '%Hd,%Ld' $path)
	local file_type_s=$(stat -f '%HT' $path)
	local gid=$(stat -f '%5g' $path)
	local groupname=$(stat -f '%8Sg' $path)
	local inode=$(stat -f '%i' $path)
	local mode=$(stat -f '%Mp%Lp' $path)
	local mode_s=$(stat -f '%Sp' $path)
	local mtime_s=$(x_output_date -f '%Sm' $path)
	local nlink=$(stat -f '%l' $path)
	local size_a=$(stat -f '%-11z' $path)
	local uid=$(stat -f '%5u' $path)
	local username=$(stat -f '%8Su' $path)

	cat <<EOF
  File: "$path"
  Size: $size_a  FileType: $file_type_s
  Mode: ($mode/$mode_s)         Uid: ($uid/$username)  Gid: ($gid/$groupname)
Device: $devid   Inode: $inode    Links: $nlink
Access: $atime_s
Modify: $mtime_s
Change: $ctime_s
EOF
}

atf_test_case x_flag
x_flag_head()
{
	atf_set	"descr" "Verify the output format for -x"
}
x_flag_body()
{
	atf_check touch a
	atf_check ln a b
	atf_check ln -s a c
	atf_check mkdir d

	paths="a b c d"

	for path in $paths; do
		atf_check -o "inline:$(x_output $path)\n" stat -x $path
	done
}

atf_init_test_cases()
{
	atf_add_test_case F_flag
	#atf_add_test_case H_flag
	#atf_add_test_case L_flag
	#atf_add_test_case f_flag
	atf_add_test_case l_flag
	atf_add_test_case n_flag
	atf_add_test_case q_flag
	atf_add_test_case r_flag
	atf_add_test_case s_flag
	atf_add_test_case t_flag
	atf_add_test_case x_flag
}
