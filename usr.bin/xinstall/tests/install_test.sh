#
# Copyright (c) 2016 Jilles Tjoelker
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
#

copy_to_nonexistent_with_opts() {
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install "$@" testf copyf
	cmp testf copyf || atf_fail "bad copy"
	[ ! testf -nt copyf ] || atf_fail "bad timestamp"
	[ ! -e copyf.bak ] || atf_fail "no backup expected"
}

atf_test_case copy_to_nonexistent
copy_to_nonexistent_body() {
	copy_to_nonexistent_with_opts
}

atf_test_case copy_to_nonexistent_safe
copy_to_nonexistent_safe_body() {
	copy_to_nonexistent_with_opts -S
}

atf_test_case copy_to_nonexistent_comparing
copy_to_nonexistent_comparing_body() {
	copy_to_nonexistent_with_opts -C
}

atf_test_case copy_to_nonexistent_safe_comparing
copy_to_nonexistent_safe_comparing_body() {
	copy_to_nonexistent_with_opts -S -C
}

atf_test_case copy_to_nonexistent_backup
copy_to_nonexistent_backup_body() {
	copy_to_nonexistent_with_opts -b -B.bak
}

atf_test_case copy_to_nonexistent_backup_safe
copy_to_nonexistent_backup_safe_body() {
	copy_to_nonexistent_with_opts -b -B.bak -S
}

atf_test_case copy_to_nonexistent_preserving
copy_to_nonexistent_preserving_body() {
	copy_to_nonexistent_with_opts -p
	[ ! testf -ot copyf ] || atf_fail "bad timestamp 2"
}

copy_self_with_opts() {
	printf 'test\n123\r456\r\n789\0z' >testf
	printf 'test\n123\r456\r\n789\0z' >testf2
	atf_check -s not-exit:0 -o empty -e match:. install "$@" testf testf
	cmp testf testf2 || atf_fail "file changed after self-copy attempt"
}

atf_test_case copy_self
copy_self_body() {
	copy_self_with_opts
}

atf_test_case copy_self_safe
copy_self_safe_body() {
	copy_self_with_opts -S
}

atf_test_case copy_self_comparing
copy_self_comparing_body() {
	copy_self_with_opts -C
}

atf_test_case copy_self_safe_comparing
copy_self_safe_comparing_body() {
	copy_self_with_opts -S -C
}

overwrite_with_opts() {
	printf 'test\n123\r456\r\n789\0z' >testf
	printf 'test\n123\r456\r\n789\0w' >otherf
	atf_check install "$@" testf otherf
	cmp testf otherf || atf_fail "bad overwrite"
	[ ! testf -nt otherf ] || atf_fail "bad timestamp"
}

atf_test_case overwrite
overwrite_body() {
	overwrite_with_opts
}

atf_test_case overwrite_safe
overwrite_safe_body() {
	overwrite_with_opts -S
}

atf_test_case overwrite_comparing
overwrite_comparing_body() {
	overwrite_with_opts -C
}

atf_test_case overwrite_safe_comparing
overwrite_safe_comparing_body() {
	overwrite_with_opts -S -C
}

overwrite_eq_with_opts() {
	printf 'test\n123\r456\r\n789\0z' >testf
	printf 'test\n123\r456\r\n789\0z' >otherf
	atf_check install "$@" testf otherf
	cmp testf otherf || atf_fail "bad overwrite"
	[ ! testf -nt otherf ] || atf_fail "bad timestamp"
}

atf_test_case overwrite_eq
overwrite_eq_body() {
	overwrite_eq_with_opts
}

atf_test_case overwrite_eq_safe
overwrite_eq_safe_body() {
	overwrite_eq_with_opts -S
}

atf_test_case overwrite_eq_comparing
overwrite_eq_comparing_body() {
	overwrite_eq_with_opts -C
}

atf_test_case overwrite_eq_safe_comparing
overwrite_eq_safe_comparing_body() {
	overwrite_eq_with_opts -S -C
}

overwrite_backup_with_opts() {
	printf 'test\n123\r456\r\n789\0z' >testf
	printf 'test\n123\r456\r\n789\0w' >otherf
	printf 'test\n123\r456\r\n789\0w' >otherf2
	atf_check install -b -B.bak "$@" testf otherf
	cmp testf otherf || atf_fail "bad overwrite"
	[ ! testf -nt otherf ] || atf_fail "bad timestamp"
	cmp otherf.bak otherf2 || atf_fail "bad backup"
}

atf_test_case overwrite_backup
overwrite_backup_body() {
	overwrite_backup_with_opts
}

atf_test_case overwrite_backup_safe
overwrite_backup_safe_body() {
	overwrite_backup_with_opts -S
}

atf_test_case overwrite_backup_comparing
overwrite_backup_comparing_body() {
	overwrite_backup_with_opts -C
}

atf_test_case overwrite_backup_safe_comparing
overwrite_backup_safe_comparing_body() {
	overwrite_backup_with_opts -S -C
}

setup_stripbin() {
	cat <<\STRIPBIN >stripbin
#!/bin/sh
tr z @ <"$1" >"$1.new" && mv -- "$1.new" "$1"
STRIPBIN
	chmod 755 stripbin
	export STRIPBIN="$PWD/stripbin"
}

strip_changing_with_opts() {
	setup_stripbin
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install -s "$@" testf copyf
	[ ! testf -nt copyf ] || atf_fail "bad timestamp"
	printf 'test\n123\r456\r\n789\0@' >otherf
	cmp otherf copyf || atf_fail "bad stripped copy"
}

atf_test_case strip_changing
strip_changing_body() {
	strip_changing_with_opts
}

atf_test_case strip_changing_comparing
strip_changing_comparing_body() {
	strip_changing_with_opts -C
}

strip_changing_overwrite_with_opts() {
	setup_stripbin
	printf 'test\n123\r456\r\n789\0z' >testf
	printf 'test\n123\r456\r\n789\0w' >copyf
	atf_check install -s "$@" testf copyf
	[ ! testf -nt copyf ] || atf_fail "bad timestamp"
	printf 'test\n123\r456\r\n789\0@' >otherf
	cmp otherf copyf || atf_fail "bad stripped copy"
}

atf_test_case strip_changing_overwrite
strip_changing_overwrite_body() {
	strip_changing_overwrite_with_opts
}

atf_test_case strip_changing_overwrite_comparing
strip_changing_overwrite_comparing_body() {
	strip_changing_overwrite_with_opts -C
}

strip_changing_overwrite_eq_with_opts() {
	setup_stripbin
	printf 'test\n123\r456\r\n789\0z' >testf
	printf 'test\n123\r456\r\n789\0@' >copyf
	atf_check install -s "$@" testf copyf
	[ ! testf -nt copyf ] || atf_fail "bad timestamp"
	printf 'test\n123\r456\r\n789\0@' >otherf
	cmp otherf copyf || atf_fail "bad stripped copy"
}

atf_test_case strip_changing_overwrite_eq
strip_changing_overwrite_eq_body() {
	strip_changing_overwrite_eq_with_opts
}

atf_test_case strip_changing_overwrite_eq_comparing
strip_changing_overwrite_eq_comparing_body() {
	strip_changing_overwrite_eq_with_opts -C
}

atf_test_case strip_noop
strip_noop_body() {
	export STRIPBIN=true
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install -s testf copyf
	[ ! testf -nt copyf ] || atf_fail "bad timestamp"
	printf 'test\n123\r456\r\n789\0z' >otherf
	cmp otherf copyf || atf_fail "bad stripped copy"
}

atf_test_case hard_link
hard_link_body() {
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install -l h testf copyf
	[ testf -ef copyf ] || atf_fail "not same file"
	[ ! -L copyf ] || atf_fail "copy is symlink"
}

atf_test_case symbolic_link
symbolic_link_body() {
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install -l s testf copyf
	[ testf -ef copyf ] || atf_fail "not same file"
	[ -L copyf ] || atf_fail "copy is not symlink"
}

atf_test_case symbolic_link_absolute
symbolic_link_absolute_body() {
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install -l sa testf copyf
	[ testf -ef copyf ] || atf_fail "not same file"
	[ -L copyf ] || atf_fail "copy is not symlink"
	copyf_path=$(readlink copyf)
	testf_path="$(pwd -P)/testf"
	if [ "$copyf_path" != "$testf_path" ]; then
		atf_fail "unexpected symlink contents ('$copyf_path' != '$testf_path')"
	fi
}

atf_test_case symbolic_link_relative
symbolic_link_relative_body() {
	printf 'test\n123\r456\r\n789\0z' >testf
	atf_check install -l sr testf copyf
	[ testf -ef copyf ] || atf_fail "not same file"
	[ -L copyf ] || atf_fail "copy is not symlink"
	copyf_path=$(readlink copyf)
	testf_path="testf"
	if [ "$copyf_path" != "$testf_path" ]; then
		atf_fail "unexpected symlink contents ('$copyf_path' != '$testf_path')"
	fi
}

atf_test_case symbolic_link_relative_absolute_source_and_dest1
symbolic_link_relative_absolute_source_and_dest1_head() {
	atf_set "descr" "Verify -l rs with absolute paths (.../copyf -> .../a/b/c/testf)"
}
symbolic_link_relative_absolute_source_and_dest1_body() {
	src_path=a/b/c/testf
	src_path_prefixed=$PWD/$src_path
	dest_path=$PWD/copyf

	atf_check mkdir -p a/b/c
	atf_check touch $src_path
	atf_check install -l sr $src_path_prefixed $dest_path
	[ $src_path_prefixed -ef $dest_path ] || atf_fail "not same file"
	[ -L $dest_path ] || atf_fail "copy is not symlink"
	dest_path_relative=$(readlink $dest_path)
	src_path_relative="$src_path"
	if [ "$src_path_relative" != "$dest_path_relative" ]; then
		atf_fail "unexpected symlink contents ('$src_path_relative' != '$dest_path_relative')"
	fi
}

atf_test_case symbolic_link_relative_absolute_source_and_dest1_double_slash
symbolic_link_relative_absolute_source_and_dest1_double_slash_head() {
	atf_set "descr" "Verify -l rs with absolute paths (.../copyf -> .../a/b/c/testf), using double-slashes"
}
symbolic_link_relative_absolute_source_and_dest1_double_slash_body() {
	src_path=a//b//c//testf
	src_path_prefixed=$PWD/$src_path
	dest_path=$PWD/copyf

	atf_check mkdir -p a/b/c
	atf_check touch $src_path
	atf_check install -l sr $src_path_prefixed $dest_path
	[ $src_path_prefixed -ef $dest_path ] || atf_fail "not same file"
	[ -L $dest_path ] || atf_fail "copy is not symlink"
	dest_path_relative=$(readlink $dest_path)
	src_path_relative="$(echo $src_path | sed -e 's,//,/,g')"
	if [ "$src_path_relative" != "$dest_path_relative" ]; then
		atf_fail "unexpected symlink contents ('$src_path_relative' != '$dest_path_relative')"
	fi
}

atf_test_case symbolic_link_relative_absolute_source_and_dest2
symbolic_link_relative_absolute_source_and_dest2_head() {
	atf_set "descr" "Verify -l rs with absolute paths (.../a/b/c/copyf -> .../testf)"
}
symbolic_link_relative_absolute_source_and_dest2_body() {
	src_path=testf
	src_path_prefixed=$PWD/$src_path
	dest_path=$PWD/a/b/c/copyf

	atf_check mkdir -p a/b/c
	atf_check touch $src_path
	atf_check install -l sr $src_path_prefixed $dest_path
	[ $src_path_prefixed -ef $dest_path ] || atf_fail "not same file"
	[ -L $dest_path ] || atf_fail "copy is not symlink"
	dest_path_relative=$(readlink $dest_path)
	src_path_relative="../../../$src_path"
	if [ "$src_path_relative" != "$dest_path_relative" ]; then
		atf_fail "unexpected symlink contents ('$src_path_relative' != '$dest_path_relative')"
	fi
}

atf_test_case mkdir_simple
mkdir_simple_body() {
	atf_check install -d dir1/dir2
	[ -d dir1 ] || atf_fail "dir1 missing"
	[ -d dir1/dir2 ] || atf_fail "dir2 missing"
	atf_check install -d dir1/dir2/dir3
	[ -d dir1/dir2/dir3 ] || atf_fail "dir3 missing"
	atf_check install -d dir1
	atf_check install -d dir1/dir2/dir3
}

atf_test_case symbolic_link_relative_absolute_common
symbolic_link_relative_absolute_common_head() {
	atf_set "descr" "Verify -l rs with absolute paths having common components"
}
symbolic_link_relative_absolute_common_body() {
	filename=foo.so
	src_path=lib
	src_path_prefixed=$PWD/$src_path
	dest_path=$PWD/libexec/
	src_file=$src_path_prefixed/$filename
	dest_file=$dest_path/$filename

	atf_check mkdir $src_path_prefixed $dest_path
	atf_check touch $src_file
	atf_check install -l sr $src_file $dest_path

	dest_path_relative=$(readlink $dest_file)
	src_path_relative="../lib/$filename"
	if [ "$src_path_relative" != "$dest_path_relative" ]; then
		atf_fail "unexpected symlink contents ('$src_path_relative' != '$dest_path_relative')"
	fi
}

atf_init_test_cases() {
	atf_add_test_case copy_to_nonexistent
	atf_add_test_case copy_to_nonexistent_safe
	atf_add_test_case copy_to_nonexistent_comparing
	atf_add_test_case copy_to_nonexistent_safe_comparing
	atf_add_test_case copy_to_nonexistent_backup
	atf_add_test_case copy_to_nonexistent_backup_safe
	atf_add_test_case copy_to_nonexistent_preserving
	atf_add_test_case copy_self
	atf_add_test_case copy_self_safe
	atf_add_test_case copy_self_comparing
	atf_add_test_case copy_self_safe_comparing
	atf_add_test_case overwrite
	atf_add_test_case overwrite_safe
	atf_add_test_case overwrite_comparing
	atf_add_test_case overwrite_safe_comparing
	atf_add_test_case overwrite_eq
	atf_add_test_case overwrite_eq_safe
	atf_add_test_case overwrite_eq_comparing
	atf_add_test_case overwrite_eq_safe_comparing
	atf_add_test_case overwrite_backup
	atf_add_test_case overwrite_backup_safe
	atf_add_test_case overwrite_backup_comparing
	atf_add_test_case overwrite_backup_safe_comparing
	atf_add_test_case strip_changing
	atf_add_test_case strip_changing_comparing
	atf_add_test_case strip_changing_overwrite
	atf_add_test_case strip_changing_overwrite_comparing
	atf_add_test_case strip_changing_overwrite_eq
	atf_add_test_case strip_changing_overwrite_eq_comparing
	atf_add_test_case strip_noop
	atf_add_test_case hard_link
	atf_add_test_case symbolic_link
	atf_add_test_case symbolic_link_absolute
	atf_add_test_case symbolic_link_relative
	atf_add_test_case symbolic_link_relative_absolute_source_and_dest1
	atf_add_test_case symbolic_link_relative_absolute_source_and_dest1_double_slash
	atf_add_test_case symbolic_link_relative_absolute_source_and_dest2
	atf_add_test_case symbolic_link_relative_absolute_common
	atf_add_test_case mkdir_simple
}
