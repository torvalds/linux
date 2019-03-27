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

# Various regression tests to test the -I flag to the 'update' command.

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: ignore.sh [-s script] [-w workdir]"
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

# These tests deal with ignoring certain patterns of files.  We run the
# test multiple times ignoring different patterns.
build_trees()
{
	local i

	rm -rf $OLD $NEW $TEST $CONFLICTS
	mkdir -p $OLD $NEW $TEST

	for i in $OLD $NEW $TEST; do
		mkdir -p $i/tree
	done

	# tree: Test three different cases (add, modify, remove) that all
	# match the tree/* glob.
	echo "foo" > $NEW/tree/add
	for i in $OLD $TEST; do
		echo "old" > $i/tree/modify
	done
	echo "new" > $NEW/tree/modify
	for i in $OLD $TEST; do
		echo "old" > $i/tree/remove
	done

	# rmdir: Remove a whole tree.
	for i in $OLD $TEST; do
		mkdir $i/rmdir
		echo "foo" > $i/rmdir/file
	done
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

# $1 - relative path to file that should be a directory in TEST
dir()
{
	if ! [ -d $TEST/$1 ]; then
		echo "File $1 should be a directory"
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

# $1 - relative path to a regular file that should not have a conflict
noconflict()
{
	if [ -f $CONFLICTS/$1 ]; then
		echo "File $1 should not have a conflict"
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

# First run the test ignoring no patterns.

build_trees

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

cat > $WORKDIR/correct.out <<EOF
  D /rmdir/file
  D /tree/remove
  D /rmdir
  U /tree/modify
  A /tree/add
EOF

echo "Differences for regular:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

missing /tree/remove
file /tree/modify "new"
file /tree/add "foo"
missing /rmdir/file
missing /rmdir

# Now test with -I '/tree/*'.  This should preserve the /tree files.

build_trees

$COMMAND -r -I '/tree/*' -d $WORKDIR -D $TEST > $WORKDIR/test1.out

cat > $WORKDIR/correct1.out <<EOF
  D /rmdir/file
  D /rmdir
EOF

echo "Differences for -I '/tree/*':"
diff -u -L "correct" $WORKDIR/correct1.out -L "test" $WORKDIR/test1.out \
    || FAILED=yes

file /tree/remove "old"
file /tree/modify "old"
missing /tree/add
missing /rmdir/file
missing /rmdir

# Now test with two patterns.  This should preserve everything.

build_trees

$COMMAND -r -I '/tree/*' -I '/rmdir*' -d $WORKDIR -D $TEST > \
    $WORKDIR/test2.out

cat > $WORKDIR/correct2.out <<EOF
EOF

echo "Differences for -I '/tree/*' -I '/rmdir*':"

diff -u -L "correct" $WORKDIR/correct2.out -L "test" $WORKDIR/test2.out \
    || FAILED=yes

file /tree/remove "old"
file /tree/modify "old"
missing /tree/add
file /rmdir/file "foo"

# Now test with a pattern that should cause a warning on /rmdir by
# only ignoring the files under that directory.  Note that this also
# tests putting two patterns into a single -I argument.

build_trees

$COMMAND -r -I '/tree/* /rmdir/*' -d $WORKDIR -D $TEST > \
    $WORKDIR/test3.out

cat > $WORKDIR/correct3.out <<EOF
Warnings:
  Non-empty directory remains: /rmdir
EOF

echo "Differences for -I '/tree/* /rmdir/*':"

diff -u -L "correct" $WORKDIR/correct3.out -L "test" $WORKDIR/test3.out \
    || FAILED=yes

file /tree/remove "old"
file /tree/modify "old"
missing /tree/add
file /rmdir/file "foo"
dir /rmdir

[ "${FAILED}" = no ]
