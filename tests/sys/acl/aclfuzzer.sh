#!/bin/sh
#
# Copyright (c) 2008, 2009 Edward Tomasz Napierała <trasz@FreeBSD.org>
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

# This is an NFSv4 ACL fuzzer.  It expects to be run by non-root in a scratch
# directory on a filesystem with NFSv4 ACLs support.  Output it generates
# is expected to be fed to /usr/src/tools/regression/acltools/run script.

NUMBER_OF_COMMANDS=300

run_command()
{
	echo "\$ $1"
	eval $1 2>&1 | sed 's/^/> /'
}

rnd_from_0_to()
{
	max=`expr $1 + 1`
	rnd=`jot -r 1`
	rnd=`expr $rnd % $max`

	echo $rnd
}

rnd_path()
{
	rnd=`rnd_from_0_to 3`
	case $rnd in
		0) echo "$TMP/aaa" ;;
		1) echo "$TMP/bbb" ;;
		2) echo "$TMP/aaa/ccc" ;;
		3) echo "$TMP/bbb/ddd" ;;
	esac
}

f_prepend_random_acl_on()
{
	rnd=`rnd_from_0_to 4`
	case $rnd in
		0) u="owner@" ;;
		1) u="group@" ;;
		2) u="everyone@" ;;
		3) u="u:1138" ;;
		4) u="g:1138" ;;
	esac

	p=""
	while :; do
		rnd=`rnd_from_0_to 30`
		if [ -n "$p" -a $rnd -ge 14 ]; then
			break;
		fi

		case $rnd in
			0) p="${p}r" ;;
			1) p="${p}w" ;;
			2) p="${p}x" ;;
			3) p="${p}p" ;;
			4) p="${p}d" ;;
			5) p="${p}D" ;;
			6) p="${p}a" ;;
			7) p="${p}A" ;;
			8) p="${p}R" ;;
			9) p="${p}W" ;;
			10) p="${p}R" ;;
			11) p="${p}c" ;;
			12) p="${p}C" ;;
			13) p="${p}o" ;;
			14) p="${p}s" ;;
		esac
	done

	f=""
	while :; do
		rnd=`rnd_from_0_to 10`
		if [ $rnd -ge 6 ]; then
			break;
		fi

		case $rnd in
			0) f="${f}f" ;;
			1) f="${f}d" ;;
			2) f="${f}n" ;;
			3) f="${f}i" ;;
		esac
	done

	rnd=`rnd_from_0_to 1`
	case $rnd in
		0) x="allow" ;;
		1) x="deny" ;;
	esac

	acl="$u:$p:$f:$x"

	file=`rnd_path`
	run_command "setfacl -a0 $acl $file"
}

f_getfacl()
{
	file=`rnd_path`
	run_command "getfacl -qn $file"
}

f_ls_mode()
{
	file=`rnd_path`
	run_command "ls -al $file | sed -n '2p' | cut -d' ' -f1"
}

f_chmod()
{
	b1=`rnd_from_0_to 7`
	b2=`rnd_from_0_to 7`
	b3=`rnd_from_0_to 7`
	b4=`rnd_from_0_to 7`
	file=`rnd_path`

	run_command "chmod $b1$b2$b3$b4 $file $2"
}

f_touch()
{
	file=`rnd_path`
	run_command "touch $file"
}

f_rm()
{
	file=`rnd_path`
	run_command "rm -f $file"
}

f_mkdir()
{
	file=`rnd_path`
	run_command "mkdir $file"
}

f_rmdir()
{
	file=`rnd_path`
	run_command "rmdir $file"
}

f_mv()
{
	from=`rnd_path`
	to=`rnd_path`
	run_command "mv -f $from $to"
}

# XXX: To be implemented: chown(8), setting times with touch(1).

switch_to_random_user()
{
	# XXX: To be implemented.
}

execute_random_command()
{
	rnd=`rnd_from_0_to 20`

	case $rnd in
		0|10|11|12|13|15) cmd=f_prepend_random_acl_on ;;
		1) cmd=f_getfacl ;;
		2) cmd=f_ls_mode ;;
		3) cmd=f_chmod ;;
		4|18|19) cmd=f_touch ;;
		5) cmd=f_rm ;;
		6|16|17) cmd=f_mkdir ;;
		7) cmd=f_rmdir ;;
		8) cmd=f_mv ;;
	esac

	$cmd "XXX"
}

echo "# Fuzzing; will stop after $NUMBER_OF_COMMANDS commands."
TMP="aclfuzzer_`dd if=/dev/random bs=1k count=1 2>/dev/null | openssl md5`"

run_command "whoami"
umask 022
run_command "umask 022"
run_command "mkdir $TMP"

i=0;
while [ "$i" -lt "$NUMBER_OF_COMMANDS" ]; do
	switch_to_random_user
	execute_random_command
	i=`expr $i + 1`
done

run_command "find $TMP -exec setfacl -a0 everyone@:rxd:allow {} \;"
run_command "rm -rfv $TMP"

echo "# Fuzzed, thank you."

