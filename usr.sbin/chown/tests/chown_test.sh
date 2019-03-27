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

get_filesystem()
{
	local mountpoint=$1

	df -T $mountpoint | tail -n 1 | cut -wf 2
}

atf_test_case RH_flag
RH_flag_head()
{
	atf_set	"descr" "Verify that setting ownership recursively via -R doesn't " \
			"affect symlinks specified via the arguments when -H " \
			"is specified"
	atf_set "require.user" "root"
}
RH_flag_body()
{
	atf_check mkdir -p A/B
	atf_check ln -s B A/C
	atf_check chown -h 42:42 A/C
	atf_check -o inline:'0:0\n0:0\n42:42\n' stat -f '%u:%g' A A/B A/C
	atf_check chown -RH 84:84 A
	atf_check -o inline:'84:84\n84:84\n84:84\n' stat -f '%u:%g' A A/B A/C
	atf_check chown -RH 126:126 A/C
	atf_check -o inline:'84:84\n126:126\n84:84\n' stat -f '%u:%g' A A/B A/C
}

atf_test_case RL_flag
RL_flag_head()
{
	atf_set	"descr" "Verify that setting ownership recursively via -R doesn't " \
			"affect symlinks specified via the arguments when -L " \
			"is specified"
	atf_set "require.user" "root"
}
RL_flag_body()
{
	atf_check mkdir -p A/B
	atf_check ln -s B A/C
	atf_check chown -h 42:42 A/C
	atf_check -o inline:'0:0\n0:0\n42:42\n' stat -f '%u:%g' A A/B A/C
	atf_check chown -RL 84:84 A
	atf_check -o inline:'84:84\n84:84\n42:42\n' stat -f '%u:%g' A A/B A/C
	atf_check chown -RL 126:126 A/C
	atf_check -o inline:'84:84\n126:126\n42:42\n' stat -f '%u:%g' A A/B A/C
}

atf_test_case RP_flag
RP_flag_head()
{
	atf_set	"descr" "Verify that setting ownership recursively via -R " \
			"doesn't affect symlinks specified via the arguments " \
			"when -P is specified"
	atf_set "require.user" "root"
}
RP_flag_body()
{
	atf_check mkdir -p A/B
	atf_check ln -s B A/C
	atf_check chown -h 42:42 A/C
	atf_check -o inline:'0:0\n0:0\n42:42\n' stat -f '%u:%g' A A/B A/C
	atf_check chown -RP 84:84 A
	atf_check -o inline:'84:84\n84:84\n84:84\n' stat -f '%u:%g' A A/B A/C
	atf_check chown -RP 126:126 A/C
	atf_check -o inline:'84:84\n84:84\n126:126\n' stat -f '%u:%g' A A/B A/C
}

atf_test_case f_flag cleanup
f_flag_head()
{
	atf_set	"descr" "Verify that setting a mode for a file with -f " \
			"doesn't emit an error message/exit with a non-zero " \
			"code"
	atf_set "require.user" "root"
}

f_flag_body()
{
	atf_check truncate -s 0 foo bar
	atf_check chown 0:0 foo bar
	case "$(get_filesystem .)" in
	zfs)
		atf_expect_fail "ZFS does not support UF_IMMUTABLE; returns EPERM"
		;;
	esac
	atf_check chflags uchg foo
	atf_check -e not-empty -s not-exit:0 chown 42:42 foo bar
	atf_check -o inline:'0:0\n42:42\n' stat -f '%u:%g' foo bar
	atf_check -s exit:0 chown -f 84:84 foo bar
	atf_check -o inline:'0:0\n84:84\n' stat -f '%u:%g' foo bar
}

f_flag_cleanup()
{
	chflags 0 foo || :
}

atf_test_case h_flag
h_flag_head()
{
	atf_set	"descr" "Verify that setting a mode for a file with -f " \
			"doesn't emit an error message/exit with a non-zero " \
			"code"
	atf_set "require.user" "root"
}

h_flag_body()
{
	atf_check truncate -s 0 foo
	atf_check -o inline:'0:0\n' stat -f '%u:%g' foo
	atf_check ln -s foo bar
	atf_check -o inline:'0:0\n0:0\n' stat -f '%u:%g' foo bar
	atf_check chown -h 42:42 bar
	atf_check -o inline:'0:0\n42:42\n' stat -f '%u:%g' foo bar
	atf_check chown 84:84 bar
	atf_check -o inline:'84:84\n42:42\n' stat -f '%u:%g' foo bar
}

atf_test_case v_flag
v_flag_head()
{
	atf_set	"descr" "Verify that setting ownership with -v emits the " \
			"file doesn't emit an error message/exit with a " \
			"non-zero code"
	atf_set "require.user" "root"
}
v_flag_body()
{
	atf_check truncate -s 0 foo bar
	atf_check chown 0:0 foo
	atf_check chown 42:42 bar
	atf_check -o 'inline:bar\n' chown -v 0:0 foo bar
	atf_check chown -v 0:0 foo bar
	for f in foo bar; do
		echo "$f: 0:0 -> 84:84";
	done > output.txt
	atf_check -o file:output.txt chown -vv 84:84 foo bar
	atf_check chown -vv 84:84 foo bar
}

md_file="md.out"
atf_test_case x_flag cleanup
x_flag_head()
{
	atf_set	"descr" "Verify that setting a mode with -x doesn't set " \
			"ownership across mountpoints"
	atf_set "require.user" "root"
}
x_flag_body()
{
	atf_check -o save:$md_file mdconfig -a -t malloc -s 20m
	if ! md_device=$(cat $md_file); then
		atf_fail "cat $md_file failed"
	fi
	atf_check -o not-empty newfs /dev/$md_device
	atf_check mkdir mnt
	atf_check mount /dev/$md_device mnt
	atf_check truncate -s 0 foo bar mnt/bazbaz
	atf_check ln -s bar mnt/barbaz
	atf_check ln -s ../foo mnt/foobaz
	cd mnt
	test_files="../foo ../bar barbaz bazbaz foobaz"
	atf_check -o inline:'0:0\n0:0\n0:0\n0:0\n0:0\n' \
	    stat -f '%u:%g' $test_files
	atf_check chown -Rx 42:42 .
	atf_check -o inline:'0:0\n0:0\n42:42\n42:42\n42:42\n' \
	    stat -f '%u:%g' $test_files
	atf_check chown -R 84:84 .
	atf_check -o inline:'0:0\n0:0\n84:84\n84:84\n84:84\n' \
	    stat -f '%u:%g' $test_files
}
x_flag_cleanup()
{
	if ! md_device=$(cat $md_file) || [ -z "$md_device" ]; then
		echo "Couldn't get device from $md_file"
		exit 0
	fi
	umount mnt
	mdconfig -d -u $md_device
}

atf_init_test_cases()
{
	atf_add_test_case RH_flag
	atf_add_test_case RL_flag
	atf_add_test_case RP_flag
	atf_add_test_case f_flag
	atf_add_test_case h_flag
	atf_add_test_case v_flag
	atf_add_test_case x_flag
}
