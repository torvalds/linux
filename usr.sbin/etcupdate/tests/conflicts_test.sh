#!/bin/sh
#
# Copyright (c) 2010 Hudson River Trading LLC
# Written by: John H. Baldwin <jhb@FreeBSD.org>
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

# Various regression tests to run for the 'resolve' command.

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: conflicts.sh [-s script] [-w workdir]"
	exit 1
}

# Allow the user to specify an alternate work directory or script.
COMMAND=etcupdate
while getopts "s:w:" option; do
	case $option in
		s)
			COMMAND="sh $OPTARG"
			;;
		w)
			WORKDIR=$OPTARG
			;;
		*)
			echo
			usage
			;;
	esac
done
shift $((OPTIND - 1))
if [ $# -ne 0 ]; then
	usage
fi

CONFLICTS=$WORKDIR/conflicts
OLD=$WORKDIR/old
NEW=$WORKDIR/current
TEST=$WORKDIR/test

# These tests deal with conflicts to a single file.  For each test, we
# generate a conflict in /etc/login.conf.  Each resolve option is tested
# to ensure it DTRT.
build_login_conflict()
{

	rm -rf $OLD $NEW $TEST $CONFLICTS
	mkdir -p $OLD/etc $NEW/etc $TEST/etc
	
	# Generate a conflict in /etc/login.conf.
	cat > $OLD/etc/login.conf <<EOF
default:\\
	:passwd_format=md5:
EOF
	cat > $NEW/etc/login.conf <<EOF
default:\\
	:passwd_format=md5:\\
	:copyright=/etc/COPYRIGHT
EOF
	cat > $TEST/etc/login.conf <<EOF
default:\\
	:passwd_format=md5:\\
        :welcome=/etc/motd:
EOF

	$COMMAND -r -d $WORKDIR -D $TEST >/dev/null
}

# This is used to verify special handling for /etc/mail/aliases and
# the newaliases warning.
build_aliases_conflict()
{

	rm -rf $OLD $NEW $TEST $CONFLICTS
	mkdir -p $OLD/etc/mail $NEW/etc/mail $TEST/etc/mail

	# Generate a conflict in /etc/mail/aliases
	cat > $OLD/etc/mail/aliases <<EOF
# root: me@my.domain

# Basic system aliases -- these MUST be present
MAILER-DAEMON: postmaster
postmaster: root
EOF
	cat > $NEW/etc/mail/aliases <<EOF
# root: me@my.domain

# Basic system aliases -- these MUST be present
MAILER-DAEMON: postmaster
postmaster: root

# General redirections for pseudo accounts
_dhcp:  root
_pflogd: root
EOF
	cat > $TEST/etc/mail/aliases <<EOF
root: someone@example.com

# Basic system aliases -- these MUST be present
MAILER-DAEMON: postmaster
postmaster: foo
EOF

	$COMMAND -r -d $WORKDIR -D $TEST >/dev/null
}

# $1 - relative path to file that should be missing from TEST
missing()
{
	if [ -e $TEST/$1 -o -L $TEST/$1 ]; then
		echo "File $1 should be missing"
		FAILED=yes
	fi
}

# $1 - relative path to file that should be present in TEST
present()
{
	if ! [ -e $TEST/$1 -o -L $TEST/$1 ]; then
		echo "File $1 should be present"
		FAILED=yes
	fi
}

# $1 - relative path to regular file that should be present in TEST
# $2 - optional string that should match file contents
# $3 - optional MD5 of the flie contents, overrides $2 if present
file()
{
	local contents sum

	if ! [ -f $TEST/$1 ]; then
		echo "File $1 should be a regular file"
		FAILED=yes
	elif [ $# -eq 2 ]; then
		contents=`cat $TEST/$1`
		if [ "$contents" != "$2" ]; then
			echo "File $1 has wrong contents"
			FAILED=yes
		fi
	elif [ $# -eq 3 ]; then
		sum=`md5 -q $TEST/$1`
		if [ "$sum" != "$3" ]; then
			echo "File $1 has wrong contents"
			FAILED=yes
		fi
	fi
}

# $1 - relative path to a regular file that should have a conflict
# $2 - optional MD5 of the conflict file contents
conflict()
{
	local sum

	if ! [ -f $CONFLICTS/$1 ]; then
		echo "File $1 missing conflict"
		FAILED=yes
	elif [ $# -gt 1 ]; then
		sum=`md5 -q $CONFLICTS/$1`
		if [ "$sum" != "$2" ]; then
			echo "Conflict $1 has wrong contents"
			FAILED=yes
		fi
	fi
}

# $1 - relative path to a regular file that should no longer have a conflict
resolved()
{
	if [ -f $CONFLICTS/$1 ]; then
		echo "Conflict $1 should be resolved"
		FAILED=yes
	fi
}

if [ `id -u` -ne 0 ]; then
	echo "must be root"
	exit 0
fi

if [ -r /etc/etcupdate.conf ]; then
	echo "WARNING: /etc/etcupdate.conf settings may break some tests."
fi

# Test each of the following resolve options: 'p', 'mf', 'tf', 'r'.

build_login_conflict

# Verify that 'p' doesn't do anything.
echo "Checking 'p':"
echo 'p' | $COMMAND resolve -d $WORKDIR -D $TEST >/dev/null

file /etc/login.conf "" 95de92ea3f1bb1bf4f612a8b5908cddd
missing /etc/login.conf.db
conflict /etc/login.conf

# Verify that 'mf' removes the conflict, but does nothing else.
echo "Checking 'mf':"
echo 'mf' | $COMMAND resolve -d $WORKDIR -D $TEST >/dev/null

file /etc/login.conf "" 95de92ea3f1bb1bf4f612a8b5908cddd
missing /etc/login.conf.db
resolved /etc/login.conf

build_login_conflict

# Verify that 'tf' installs the new version of the file.
echo "Checking 'tf':"
echo 'tf' | $COMMAND resolve -d $WORKDIR -D $TEST >/dev/null

file /etc/login.conf "" 7774a0f9a3a372c7c109c32fd31c4b6b
file /etc/login.conf.db
resolved /etc/login.conf

build_login_conflict

# Verify that 'r' installs the resolved version of the file.  To
# simulate this, manually edit the merged file so that it doesn't
# contain conflict markers.
echo "Checking 'r':"
cat > $CONFLICTS/etc/login.conf <<EOF
default:\\
	:passwd_format=md5:\\
	:copyright=/etc/COPYRIGHT\\
        :welcome=/etc/motd:
EOF

echo 'r' | $COMMAND resolve -d $WORKDIR -D $TEST >/dev/null

file /etc/login.conf "" 966e25984b9b63da8eaac8479dcb0d4d
file /etc/login.conf.db
resolved /etc/login.conf

build_aliases_conflict

# Verify that 'p' and 'mf' do not generate the newaliases warning.
echo "Checking newalias warning for 'p'":
echo 'p' | $COMMAND resolve -d $WORKDIR -D $TEST | grep -q newalias
if [ $? -eq 0 ]; then
	echo "+ Extra warning"
	FAILED=yes
fi
echo "Checking newalias warning for 'mf'":
echo 'mf' | $COMMAND resolve -d $WORKDIR -D $TEST | grep -q newalias
if [ $? -eq 0 ]; then
	echo "+ Extra warning"
	FAILED=yes
fi

# Verify that 'tf' and 'r' do generate the newaliases warning.
build_aliases_conflict
echo "Checking newalias warning for 'tf'":
echo 'tf' | $COMMAND resolve -d $WORKDIR -D $TEST | grep -q newalias
if [ $? -ne 0 ]; then
	echo "- Missing warning"
	FAILED=yes
fi

build_aliases_conflict
cp $TEST/etc/mail/aliases $CONFLICTS/etc/mail/aliases
echo 'r' | $COMMAND resolve -d $WORKDIR -D $TEST | grep -q newalias
if [ $? -ne 0 ]; then
	echo "- Missing warning"
	FAILED=yes
fi

[ "${FAILED}" = no ]
