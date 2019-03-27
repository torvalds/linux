#!/bin/sh
#
# Copyright (c) 2013 Hudson River Trading LLC
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

# Various regression tests for the tzsetup handling in the 'update' command.

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: tzsetup.sh [-s script] [-w workdir]"
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

build_trees()
{

	# Build the base tree, but not /etc/localtime itself
	local i j k

	rm -rf $OLD $NEW $TEST $CONFLICTS
	mkdir -p $OLD $NEW $TEST
	mkdir -p $TEST/etc
	mkdir -p $TEST/var/db
	mkdir -p $TEST/usr/share/zoneinfo

	# Create a dummy timezone file
	echo "foo" > $TEST/usr/share/zoneinfo/foo

}

# $1 - relative path to file that should be missing from TEST
missing()
{
	if [ -e $TEST/$1 -o -L $TEST/$1 ]; then
		echo "File $1 should be missing"
		FAILED=yes
	fi
}

# $1 - relative path to file that should be a symlink in TEST
# $2 - optional value of the link
link()
{
	local val

	if ! [ -L $TEST/$1 ]; then
		echo "File $1 should be a link"
		FAILED=yes
	elif [ $# -gt 1 ]; then
		val=`readlink $TEST/$1`
		if [ "$val" != "$2" ]; then
			echo "Link $1 should link to \"$2\""
			FAILED=yes
		fi
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

if [ `id -u` -ne 0 ]; then
	echo "must be root"
	exit 0
fi

if [ -r /etc/etcupdate.conf ]; then
	echo "WARNING: /etc/etcupdate.conf settings may break some tests."
fi

# First, test for /etc/localtime not existing

build_trees

$COMMAND -nr -d $WORKDIR -D $TEST > $WORKDIR/testn.out

cat > $WORKDIR/correct.out <<EOF
EOF

echo "Differences for no /etc/localtime with -n:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/testn.out \
    || FAILED=yes

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

echo "Differences for no /etc/localtime:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

missing /etc/localtime
missing /var/db/zoneinfo

# Second, test for /etc/localtime being a symlink

build_trees
ln -s /dev/null $TEST/etc/localtime

$COMMAND -nr -d $WORKDIR -D $TEST > $WORKDIR/testn.out

cat > $WORKDIR/correct.out <<EOF
EOF

echo "Differences for symlinked /etc/localtime with -n:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/testn.out \
    || FAILED=yes

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

echo "Differences for symlinked /etc/localtime:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

link /etc/localtime "/dev/null"
missing /var/db/zoneinfo

# Third, test for /etc/localtime as a file and a missing /var/db/zoneinfo

build_trees
echo "bar" > $TEST/etc/localtime

$COMMAND -nr -d $WORKDIR -D $TEST > $WORKDIR/testn.out

cat > $WORKDIR/correct.out <<EOF
Warnings:
  Needs update: /etc/localtime (required manual update via tzsetup(8))
EOF

echo "Differences for missing /var/db/zoneinfo with -n:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/testn.out \
    || FAILED=yes

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

echo "Differences for missing /var/db/zoneinfo:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

file /etc/localtime "bar"
missing /var/db/zoneinfo

# Finally, test the case where it should update /etc/localtime

build_trees
echo "bar" > $TEST/etc/localtime
echo "foo" > $TEST/var/db/zoneinfo

$COMMAND -nr -d $WORKDIR -D $TEST > $WORKDIR/testn.out

cat > $WORKDIR/correct.out <<EOF
EOF

echo "Differences for real update with -n:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/testn.out \
    || FAILED=yes

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

echo "Differences for real update:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

file /etc/localtime "foo"
file /var/db/zoneinfo "foo"

[ "${FAILED}" = no ]
