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

# Various regression tests to test the -A flag to the 'update' command.

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: always.sh [-s script] [-w workdir]"
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

# The various states of the comparison of a file between two trees.
states="equal first second difftype difflinks difffiles"

# These tests deal with ignoring certain patterns of files.  We run
# the test multiple times forcing the install of different patterns.
build_trees()
{
	local i

	rm -rf $OLD $NEW $TEST $CONFLICTS

	for i in $states; do
		for j in $states; do
			for k in $states; do
				mkdir -p $OLD/$i/$j/$k $NEW/$i/$j/$k \
				    $TEST/$i/$j/$k
			done
		done
	done

	# What follows are the various warning/conflict cases from the
	# larger regression tests.  These results of many of these
	# tests should be changed when installation is forced.  The
	# cases when these updates should still fail even when forced
	# are: 1) it should not force the removal of a modified file
	# and 2) it should not remove a subdirectory that contains a
	# modified or added file.

	# /first/difftype/second: File with different local type
	# removed.  Should generate a warning.
	mkfifo $OLD/first/difftype/second/fifo
	mkdir $TEST/first/difftype/second/fifo

	# /first/difflinks/second: Modified link removed.  Should
	# generate a warning.
	ln -s "old link" $OLD/first/difflinks/second/link
	ln -s "test link" $TEST/first/difflinks/second/link

	# /first/difffiles/second: Modified file removed.  Should
	# generate a warning.
	echo "foo" > $OLD/first/difffiles/second/file
	echo "bar" > $TEST/first/difffiles/second/file

	# /second/second/difftype: Newly added file conflicts with
	# existing file in test tree of a different type.  Should
	# generate a warning.
	mkdir $NEW/second/second/difftype/dir
	mkfifo $TEST/second/second/difftype/dir

	# /second/second/difflinks: Newly added link conflicts with
	# existing link in test tree.  Should generate a warning.
	ln -s "new link" $NEW/second/second/difflinks/link
	ln -s "test link" $TEST/second/second/difflinks/link

	# /second/second/difffiles: Newly added file conflicts with
	# existing file in test tree.  Should generate a warning.
	echo "new" > $NEW/second/second/difffiles/file
	echo "test" > $TEST/second/second/difffiles/file

	# /difftype/first/first: A removed file has changed type.
	# This should generate a warning.
	mkfifo $OLD/difftype/first/first/fifo
	mkdir $NEW/difftype/first/first/fifo

	# /difftype/difftype/difftype: All three files (old, new, and
	# test) are different types from each other.  This should
	# generate a warning.
	mkfifo $OLD/difftype/difftype/difftype/one
	mkdir $NEW/difftype/difftype/difftype/one
	echo "foo" > $TEST/difftype/difftype/difftype/one
	mkdir $OLD/difftype/difftype/difftype/two
	echo "baz" > $NEW/difftype/difftype/difftype/two
	ln -s "bar" $TEST/difftype/difftype/difftype/two

	# /difftype/difftype/difflinks: A file has changed from a
	# non-link to a link in both the new and test trees, but the
	# target of the new and test links differ.  This should
	# generate a new link conflict.
	mkfifo $OLD/difftype/difftype/difflinks/link
	ln -s "new" $NEW/difftype/difftype/difflinks/link
	ln -s "test" $TEST/difftype/difftype/difflinks/link

	# /difftype/difftype/difffile: A file has changed from a
	# non-regular file to a regular file in both the new and test
	# trees, but the contents in the new and test files differ.
	# This should generate a new file conflict.
	ln -s "old" $OLD/difftype/difftype/difffiles/file
	echo "foo" > $NEW/difftype/difftype/difffiles/file
	echo "bar" > $TEST/difftype/difftype/difffiles/file

	# /difflinks/first/first: A modified link is missing in the
	# test tree.  This should generate a warning.
	ln -s "old" $OLD/difflinks/first/first/link
	ln -s "new" $NEW/difflinks/first/first/link

	# /difflinks/difftype/difftype: An updated link has been
	# changed to a different file type in the test tree.  This
	# should generate a warning.
	ln -s "old" $OLD/difflinks/difftype/difftype/link
	ln -s "new" $NEW/difflinks/difftype/difftype/link
	echo "test" > $TEST/difflinks/difftype/difftype/link

	# /difflinks/difflinks/difflinks: An updated link has been
	# modified in the test tree and doesn't match either the old
	# or new links.  This should generate a warning.
	ln -s "old" $OLD/difflinks/difflinks/difflinks/link
	ln -s "new" $NEW/difflinks/difflinks/difflinks/link
	ln -s "test" $TEST/difflinks/difflinks/difflinks/link

	# /difffiles/first/first: A removed file has been changed in
	# the new tree.  This should generate a warning.
	echo "foo" > $OLD/difffiles/first/first/file
	echo "bar" > $NEW/difffiles/first/first/file

	# /difffiles/difftype/difftype: An updated regular file has
	# been changed to a different file type in the test tree.
	# This should generate a warning.
	echo "old" > $OLD/difffiles/difftype/difftype/file
	echo "new" > $NEW/difffiles/difftype/difftype/file
	mkfifo $TEST/difffiles/difftype/difftype/file

	# /difffiles/difffiles/difffiles: A modified regular file was
	# updated in the new tree.  The changes should be merged into
	# to the new file if possible.  If the merge fails, a conflict
	# should be generated.  For this test we just include the
	# conflict case.
	cat > $OLD/difffiles/difffiles/difffiles/conflict <<EOF
this is an old file
EOF
	cat > $NEW/difffiles/difffiles/difffiles/conflict <<EOF
this is a new file
EOF
	cat > $TEST/difffiles/difffiles/difffiles/conflict <<EOF
this is a test file
EOF

	## Tests for adding directories
	mkdir -p $OLD/adddir $NEW/adddir $TEST/adddir

	# /adddir/conflict: Add a new file in a directory that already
	# exists as a file.  This should generate two warnings.
	mkdir $NEW/adddir/conflict
	touch $NEW/adddir/conflict/newfile
	touch $TEST/adddir/conflict

	## Tests for removing directories
	mkdir -p $OLD/rmdir $NEW/rmdir $TEST/rmdir

	# /rmdir/extra: Do not remove a directory with an extra local file.
	# This should generate a warning.
	for i in $OLD $TEST; do
		mkdir $i/rmdir/extra
	done
	echo "foo" > $TEST/rmdir/extra/localfile.txt

	# /rmdir/conflict: Do not remove a directory with a conflicted
	# remove file.  This should generate a warning.
	for i in $OLD $TEST; do
		mkdir $i/rmdir/conflict
	done
	mkfifo $OLD/rmdir/conflict/difftype
	mkdir $TEST/rmdir/conflict/difftype

	## Tests for converting files to directories and vice versa
	for i in $OLD $NEW $TEST; do
		for j in already old fromdir todir; do
			mkdir -p $i/dirchange/$j
		done
	done

	# /dirchange/fromdir/extradir: Convert a directory tree to a
	# file.  The test tree includes an extra file in the directory
	# that is not present in the old tree.  This should generate a
	# warning.
	for i in $OLD $TEST; do
		mkdir $i/dirchange/fromdir/extradir
		echo "foo" > $i/dirchange/fromdir/extradir/file
	done
	mkfifo $TEST/dirchange/fromdir/extradir/fifo
	ln -s "bar" $NEW/dirchange/fromdir/extradir

	# /dirchange/fromdir/conflict: Convert a directory tree to a
	# file.  The test tree includes a local change that generates
	# a warning and prevents the removal of the directory.
	for i in $OLD $TEST; do
		mkdir $i/dirchange/fromdir/conflict
	done
	echo "foo" > $OLD/dirchange/fromdir/conflict/somefile
	echo "bar" > $TEST/dirchange/fromdir/conflict/somefile
	mkfifo $NEW/dirchange/fromdir/conflict

	# /dirchange/todir/difffile: Convert a file to a directory
	# tree.  The test tree has a locally modified version of the
	# file so that the conversion fails with a warning.
	echo "foo" > $OLD/dirchange/todir/difffile
	mkdir $NEW/dirchange/todir/difffile
	echo "baz" > $NEW/dirchange/todir/difffile/file
	echo "bar" > $TEST/dirchange/todir/difffile

	# /dirchange/todir/difftype: Similar to the previous test, but
	# the conflict is due to a change in the file type.
	echo "foo" > $OLD/dirchange/todir/difftype
	mkdir $NEW/dirchange/todir/difftype
	echo "baz" > $NEW/dirchange/todir/difftype/file
	mkfifo $TEST/dirchange/todir/difftype
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

# $1 - relative path to file that should be a fifo in TEST
fifo()
{
	if ! [ -p $TEST/$1 ]; then
		echo "File $1 should be a FIFO"
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
  D /dirchange/fromdir/extradir/file
  C /difffiles/difffiles/difffiles/conflict
  C /difftype/difftype/difffiles/file
  C /second/second/difffiles/file
Warnings:
  Modified regular file remains: /dirchange/fromdir/conflict/somefile
  Modified regular file remains: /first/difffiles/second/file
  Modified symbolic link remains: /first/difflinks/second/link
  Modified directory remains: /first/difftype/second/fifo
  Modified directory remains: /rmdir/conflict/difftype
  Non-empty directory remains: /rmdir/extra
  Non-empty directory remains: /rmdir/conflict
  Modified mismatch: /difffiles/difftype/difftype/file (regular file vs fifo file)
  Removed file changed: /difffiles/first/first/file
  Modified link changed: /difflinks/difflinks/difflinks/link ("old" became "new")
  Modified mismatch: /difflinks/difftype/difftype/link (symbolic link vs regular file)
  Removed link changed: /difflinks/first/first/link ("old" became "new")
  New link conflict: /difftype/difftype/difflinks/link ("new" vs "test")
  Modified regular file changed: /difftype/difftype/difftype/one (fifo file became directory)
  Modified symbolic link changed: /difftype/difftype/difftype/two (directory became regular file)
  Remove mismatch: /difftype/first/first/fifo (fifo file became directory)
  Modified directory changed: /dirchange/fromdir/conflict (directory became fifo file)
  Modified directory changed: /dirchange/fromdir/extradir (directory became symbolic link)
  Modified regular file changed: /dirchange/todir/difffile (regular file became directory)
  Modified fifo file changed: /dirchange/todir/difftype (regular file became directory)
  New file mismatch: /adddir/conflict (directory vs regular file)
  Directory mismatch: $TEST/adddir/conflict (regular file)
  Directory mismatch: $TEST/dirchange/todir/difffile (regular file)
  Directory mismatch: $TEST/dirchange/todir/difftype (fifo file)
  New link conflict: /second/second/difflinks/link ("new link" vs "test link")
  New file mismatch: /second/second/difftype/dir (directory vs fifo file)
EOF

echo "Differences for regular:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

## /first/difftype/second:
present /first/difftype/second/fifo

## /first/difflinks/second:
link /first/difflinks/second/link "test link"

## /first/difffiles/second:
file /first/difffiles/second/file "bar"

## /second/second/difftype:
fifo /second/second/difftype/dir

## /second/second/difflinks:
link /second/second/difflinks/link "test link"

## /second/second/difffiles:
file /second/second/difffiles/file "test"
conflict /second/second/difffiles/file 4f2ee8620a251fd53f06bb6112eb6ffa

## /difftype/first/first:
missing /difftype/first/first/fifo

## /difftype/difftype/difftype:
file /difftype/difftype/difftype/one "foo"
link /difftype/difftype/difftype/two "bar"

## /difftype/difftype/difflinks:
link /difftype/difftype/difflinks/link "test"

## /difftype/difftype/difffile:
conflict /difftype/difftype/difffiles/file 117f2bcd1f6491f6044e79e5a57a9229

## /difflinks/first/first:
missing /difflinks/first/first/link

## /difflinks/difftype/difftype:
file /difflinks/difftype/difftype/link "test"

## /difflinks/difflinks/difflinks:
link /difflinks/difflinks/difflinks/link "test"

## /difffiles/first/first:
missing /difffiles/first/first/file

## /difffiles/difftype/difftype:
fifo /difffiles/difftype/difftype/file

## /difffiles/difffiles/difffiles:
file /difffiles/difffiles/difffiles/conflict "this is a test file"
conflict /difffiles/difffiles/difffiles/conflict \
    8261cfdd89280c4a6c26e4ac86541fe9

## /adddir/conflict:
file /adddir/conflict

## /rmdir/extra:
dir /rmdir/extra
file /rmdir/extra/localfile.txt "foo"

## /rmdir/conflict:
dir /rmdir/conflict/difftype
present /rmdir/conflict

## /dirchange/fromdir/extradir:
missing /dirchange/fromdir/extradir/file
fifo /dirchange/fromdir/extradir/fifo

## /dirchange/fromdir/conflict:
file /dirchange/fromdir/conflict/somefile "bar"

## /dirchange/todir/difffile:
file /dirchange/todir/difffile "bar"

## /dirchange/todir/difftype:
fifo /dirchange/todir/difftype

# Now test with -A '/first*' -A '/second* /*di*'.  This should remove
# most of the warnings and conflicts.

build_trees

$COMMAND -r -A '/first*' -A '/second* /*di*' -d $WORKDIR -D $TEST > \
    $WORKDIR/test1.out

cat > $WORKDIR/correct1.out <<EOF
  D /dirchange/fromdir/extradir/file
  U /difffiles/difffiles/difffiles/conflict
  U /difffiles/difftype/difftype/file
  A /difffiles/first/first/file
  U /difflinks/difflinks/difflinks/link
  U /difflinks/difftype/difftype/link
  A /difflinks/first/first/link
  U /difftype/difftype/difffiles/file
  U /difftype/difftype/difflinks/link
  D /difftype/difftype/difftype/one
  U /difftype/difftype/difftype/two
  U /dirchange/todir/difffile
  U /dirchange/todir/difftype
  U /adddir/conflict
  A /adddir/conflict/newfile
  A /dirchange/todir/difffile/file
  A /dirchange/todir/difftype/file
  U /second/second/difffiles/file
  U /second/second/difflinks/link
  D /second/second/difftype/dir
Warnings:
  Modified regular file remains: /dirchange/fromdir/conflict/somefile
  Modified regular file remains: /first/difffiles/second/file
  Modified symbolic link remains: /first/difflinks/second/link
  Modified directory remains: /first/difftype/second/fifo
  Modified directory remains: /rmdir/conflict/difftype
  Non-empty directory remains: /rmdir/extra
  Non-empty directory remains: /rmdir/conflict
  Modified directory changed: /dirchange/fromdir/conflict (directory became fifo file)
  Modified directory changed: /dirchange/fromdir/extradir (directory became symbolic link)
EOF

echo "Differences for -A '/first*' -A '/second* /*di*':"
diff -u -L "correct" $WORKDIR/correct1.out -L "test" $WORKDIR/test1.out \
    || FAILED=yes

## /first/difftype/second:
present /first/difftype/second/fifo

## /first/difflinks/second:
link /first/difflinks/second/link "test link"

## /first/difffiles/second:
file /first/difffiles/second/file "bar"

## /second/second/difftype:
missing /second/second/difftype/dir

## /second/second/difflinks:
link /second/second/difflinks/link "new link"

## /second/second/difffiles:
file /second/second/difffiles/file "new"
noconflict /second/second/difffiles/file

## /difftype/first/first:
missing /difftype/first/first/fifo

## /difftype/difftype/difftype:
missing /difftype/difftype/difftype/one
file /difftype/difftype/difftype/two "baz"

## /difftype/difftype/difflinks:
link /difftype/difftype/difflinks/link "new"

## /difftype/difftype/difffile:
noconflict /difftype/difftype/difffiles/file
file /difftype/difftype/difffiles/file "foo"

## /difflinks/first/first:
link /difflinks/first/first/link "new"

## /difflinks/difftype/difftype:
link /difflinks/difftype/difftype/link "new"

## /difflinks/difflinks/difflinks:
link /difflinks/difflinks/difflinks/link "new"

## /difffiles/first/first:
file /difffiles/first/first/file "bar"

## /difffiles/difftype/difftype:
file /difffiles/difftype/difftype/file "new"

## /difffiles/difffiles/difffiles:
noconflict /difffiles/difffiles/difffiles/conflict
file /difffiles/difffiles/difffiles/conflict "this is a new file"

## /adddir/conflict:
file /adddir/conflict/newfile

## /rmdir/extra:
dir /rmdir/extra
file /rmdir/extra/localfile.txt "foo"

## /rmdir/conflict:
dir /rmdir/conflict/difftype
present /rmdir/conflict

## /dirchange/fromdir/extradir:
missing /dirchange/fromdir/extradir/file
fifo /dirchange/fromdir/extradir/fifo

## /dirchange/fromdir/conflict:
file /dirchange/fromdir/conflict/somefile "bar"

## /dirchange/todir/difffile:
file /dirchange/todir/difffile/file "baz"

## /dirchange/todir/difftype:
file /dirchange/todir/difftype/file "baz"

[ "${FAILED}" = no ]
