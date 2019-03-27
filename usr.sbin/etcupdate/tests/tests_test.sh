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

# Various regression tests to run for the 'update' command.

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: tests.sh [-s script] [-w workdir]"
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

build_trees()
{
	local i j k

	rm -rf $OLD $NEW $TEST $CONFLICTS
	mkdir -p $OLD/etc $NEW/etc $TEST/etc

	# For an given file, there are three different pair-wise
	# relations between the three threes (old, new, and test): old
	# vs new, old vs test, and new vs test.  Each of these
	# relations takes on one of six different states from the
	# 'compare()' function in etcupdate: equal, onlyfirst,
	# onlysecond, difftype, difflinks, difffiles.  In addition,
	# there are special considerations for considering cases such
	# as a file merge that results in conflicts versus one that
	# does not, special treatment of directories, etc.  The tests
	# below attempt to enumerate the three dimensional test matrix
	# by having the path name use the three different tree states
	# for the parent directories.
	#
	# Note that if the old and new files are identical (so first
	# compare is "equal"), then the second and third comparisons
	# will be the same.
	#
	# Note also that etcupdate only cares about files that are
	# present in at least one of the old or new trees.  Thus, none
	# of the '*/second/second' cases are relevant.

	for i in $states; do
		for j in $states; do
			for k in $states; do
				mkdir -p $OLD/$i/$j/$k $NEW/$i/$j/$k \
				    $TEST/$i/$j/$k
			done
		done
	done

	# /equal/equal/equal: Everything is equal.  Nothing should happen.
	for i in $OLD $NEW $TEST; do
		mkfifo $i/equal/equal/equal/fifo
		echo "foo" > $i/equal/equal/equal/file
		mkdir $i/equal/equal/equal/dir
		ln -s "bar" $i/equal/equal/equal/link
	done

	# /equal/first/first: The file is missing from the test
	# directory.  Nothing should happen.
	for i in $OLD $NEW; do
		mkfifo $i/equal/first/first/fifo
		echo "foo" > $i/equal/first/first/file
		mkdir $i/equal/first/first/dir
		ln -s "bar" $i/equal/first/first/link
	done

	# /equal/difftype/difftype: The local file is a different
	# type.  Nothing should happen.
	for i in $OLD $NEW; do
		mkfifo $i/equal/difftype/difftype/fifo
		mkdir $i/equal/difftype/difftype/fromdir
	done
	echo "bar" > $TEST/equal/difftype/difftype/fifo
	ln -s "test" $TEST/equal/difftype/difftype/fromdir

	# /equal/difflinks/difflinks: The local file is a modified
	# link. Nothing should happen.
	for i in $OLD $NEW; do
		ln -s "foo" $i/equal/difflinks/difflinks/link
	done
	ln -s "bar" $TEST/equal/difflinks/difflinks/link

	# /equal/difffiles/difffiles: The local file is a modified
	# file.  Nothing should happen.
	for i in $OLD $NEW; do
		echo "foo" > $i/equal/difffiles/difffiles/file
	done
	echo "bar" > $TEST/equal/difffiles/difffiles/file

	# /first/equal/second: Remove unmodified files.  The files
	# should all be removed.
	for i in $OLD $TEST; do
		mkfifo $i/first/equal/second/fifo
		echo "foo" > $i/first/equal/second/file
		mkdir $i/first/equal/second/emptydir
		ln -s "bar" $i/first/equal/second/link
		mkdir $i/first/equal/second/fulldir
		echo "foo" > $i/first/equal/second/fulldir/file
	done

	# /first/equal/*: Cannot occur.  If the file is missing from
	# new, then new vs test will always be 'second'.

	# /first/first/equal: Removed files are already removed.
	# Nothing should happen.
	mkfifo $OLD/first/first/equal/fifo
	echo "foo" > $OLD/first/first/equal/file
	mkdir $OLD/first/first/equal/dir
	ln -s "bar" $OLD/first/first/equal/link

	# /first/first/*: Cannot occur.  The files are missing from
	# both new and test.

	# /first/second/*: Cannot happen, if the file is in old for
	# old vs new, it cannot be missing for old vs test.

	# /first/difftype/second: File with different local type
	# removed.  Should generate a warning.
	mkfifo $OLD/first/difftype/second/fifo
	mkdir $TEST/first/difftype/second/fifo

	# /first/difftype/*: Cannot happen since the file is missing
	# from new but present in test.

	# /first/difflinks/second: Modified link removed.  Should
	# generate a warning.
	ln -s "old link" $OLD/first/difflinks/second/link
	ln -s "test link" $TEST/first/difflinks/second/link

	# /first/difflinks/*: Cannot happen since the file is missing
	# from new but present in test.

	# /first/difffiles/second: Modified file removed.  Should
	# generate a warning.
	echo "foo" > $OLD/first/difffiles/second/file
	echo "bar" > $TEST/first/difffiles/second/file

	# /first/difffiles/*: Cannot happen since the file is missing
	# from new but present in test.

	# /second/equal/first: Added a new file that isn't present in
	# test.  The empty directory should be ignored.
	echo "bar" > $NEW/second/equal/first/file
	mkfifo $NEW/second/equal/first/fifo
	ln -s "new" $NEW/second/equal/first/link
	mkdir $NEW/second/equal/first/emptydir
	mkdir $NEW/second/equal/first/fulldir
	echo "foo" > $NEW/second/equal/first/fulldir/file

	# /second/equal/*: Cannot happen since the file is missing
	# from test but present in new.

	# /second/first/*: Cannot happen since the file is missing
	# from old.

	# /second/second/equal: Newly added file is already present in
	# the test directory and identical to the new file.  Nothing
	# should happen.
	for i in $NEW $TEST; do
		mkfifo $i/second/second/equal/fifo
		echo "foo" > $i/second/second/equal/file
		mkdir $i/second/second/equal/dir
		ln -s "bar" $i/second/second/equal/link
	done

	# /second/second/first: Cannot happen.  The file is in dest in
	# the second test, so it can't be missing from the third test.

	# /second/second/second: Cannot happen.  The file is in new in
	# the first test, so it can't be missing from the third test.

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

	# /second/difftype/*: Cannot happen since the file is missing
	# from old.

	# /second/difflinks/*: Cannot happen since the file is missing
	# from old.

	# /second/difffiles/*: Cannot happen since the file is missing
	# from old.

	# /difftype/equal/difftype: Unmodified file has changed type.
	# File should be updated to the new file.  In the 'todir' case
	# the directory won't actually be created because it is empty.
	for i in $OLD $TEST; do
		echo "foo" > $i/difftype/equal/difftype/file
		mkdir $i/difftype/equal/difftype/fromdir
		ln -s "old" $i/difftype/equal/difftype/todir
	done
	ln -s "test" $NEW/difftype/equal/difftype/file
	mkfifo $NEW/difftype/equal/difftype/fromdir
	mkdir $NEW/difftype/equal/difftype/todir

	# /difftype/equal/*: Cannot happen.  Since the old file is a
	# difftype from the new file and the test file is identical to
	# the old file, the test file must be a difftype from the new
	# file.

	# /difftype/first/first: A removed file has changed type.
	# This should generate a warning.
	mkfifo $OLD/difftype/first/first/fifo
	mkdir $NEW/difftype/first/first/fifo

	# /difftype/first/*: Cannot happen.  Since the new file exists
	# and the dest file is missing, the last test must be 'first'.

	# /difftype/second/*: Cannot happen.  The old file exists in
	# the first test, so it cannot be missing in the second test.

	# /difftype/difftype/equal: A file has changed type, but the
	# file in the test directory already matches the new file.  Do
	# nothing.
	echo "foo" > $OLD/difftype/difftype/equal/fifo
	mkfifo $OLD/difftype/difftype/equal/file
	for i in $NEW $TEST; do
		mkfifo $i/difftype/difftype/equal/fifo
		echo "bar" > $i/difftype/difftype/equal/file
	done

	# /difftype/difftype/first: Cannot happen.  The dest file
	# exists in the second test.

	# /difftype/difftype/second: Cannot happen.  The new file
	# exists in the first test.

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

	# /difflinks/equal/difflinks: An unmodified symlink has
	# changed.  The link should be updated.
	for i in $OLD $TEST; do
		ln -s "old" $i/difflinks/equal/difflinks/link
	done
	ln -s "new" $NEW/difflinks/equal/difflinks/link

	# /difflinks/equal/*: Cannot happen.  Since old is identical
	# to test, the third test must be 'difflinks'.

	# /difflinks/first/first: A modified link is missing in the
	# test tree.  This should generate a warning.
	ln -s "old" $OLD/difflinks/first/first/link
	ln -s "new" $NEW/difflinks/first/first/link

	# /difflinks/first/*: Cannot happen.  Since the test file is
	# missing in the second test, it must be missing in the third
	# test.

	# /difflinks/second/*: Cannot happen.  The old link is present
	# in the first test, so it cannot be missing in the second
	# test.

	# /difflinks/difftype/difftype: An updated link has been
	# changed to a different file type in the test tree.  This
	# should generate a warning.
	ln -s "old" $OLD/difflinks/difftype/difftype/link
	ln -s "new" $NEW/difflinks/difftype/difftype/link
	echo "test" > $TEST/difflinks/difftype/difftype/link

	# /difflinks/difftype/*: Cannot happen.  The old and new files
	# are both links and the test file is not a link, so the third
	# test must be 'difftype'.

	# /difflinks/difflinks/equal: An updated link has already been
	# updated to the new target in the test tree.  Nothing should
	# happen.
	ln -s "old" $OLD/difflinks/difflinks/equal/link
	for i in $NEW $TEST; do
		ln -s "new" $i/difflinks/difflinks/equal/link
	done

	# /difflinks/difflinks/difflinks: An updated link has been
	# modified in the test tree and doesn't match either the old
	# or new links.  This should generate a warning.
	ln -s "old" $OLD/difflinks/difflinks/difflinks/link
	ln -s "new" $NEW/difflinks/difflinks/difflinks/link
	ln -s "test" $TEST/difflinks/difflinks/difflinks/link

	# /difflinks/difflinks/*: Cannot happen.  All three files are
	# links from the first two tests, so the third test can only
	# be 'equal' or 'difflink'.

	# /difflinks/difffiles/*: Cannot happen.  The old file is a
	# link in the first test, so it cannot be a regular file in
	# the second.

	# /difffiles/equal/difffiles: An unmodified file has been
	# changed in new tree.  The file should be updated to the new
	# version.
	for i in $OLD $TEST; do
		echo "foo" > $i/difffiles/equal/difffiles/file
	done
	echo "bar" > $NEW/difffiles/equal/difffiles/file

	# /difffiles/equal/*: Cannot happen.  Since the old file is
	# identical to the test file, the third test must be
	# 'difffiles'.

	# /difffiles/first/first: A removed file has been changed in
	# the new tree.  This should generate a warning.
	echo "foo" > $OLD/difffiles/first/first/file
	echo "bar" > $NEW/difffiles/first/first/file

	# /difffiles/first/*: Cannot happen.  The new file is a
	# regular file from the first test and the test file is
	# missing in the second test, so the third test must be
	# 'first'.

	# /difffiles/second/*: Cannot happen.  The old file is present
	# in the first test, so it must be present in the second test.

	# /difffiles/difftype/difftype: An updated regular file has
	# been changed to a different file type in the test tree.
	# This should generate a warning.
	echo "old" > $OLD/difffiles/difftype/difftype/file
	echo "new" > $NEW/difffiles/difftype/difftype/file
	mkfifo $TEST/difffiles/difftype/difftype/file

	# /difffiles/difftype/*: Cannot happen.  The new file is known
	# to be a regular file from the first test, and the test file
	# is known to exist as a different file type from the second
	# test.  The third test must be 'difftype'.

	# /difffiles/difflink/*: Cannot happen.  The old file is known
	# to be a regular file from the first test, so it cannot be a
	# link in the second test.

	# /difffiles/difffiles/equal: An updated regular file has
	# already been updated to match the new file in the test tree.
	# Nothing should happen.
	echo "foo" > $OLD/difffiles/difffiles/equal/file
	for i in $NEW $TEST; do
		echo "bar" > $i/difffiles/difffiles/equal/file
	done

	# /difffiles/difffiles/difffiles: A modified regular file was
	# updated in the new tree.  The changes should be merged into
	# to the new file if possible.  If the merge fails, a conflict
	# should be generated.
	cat > $OLD/difffiles/difffiles/difffiles/simple <<EOF
this is an old line

EOF
	cat > $NEW/difffiles/difffiles/difffiles/simple <<EOF
this is a new line

EOF
	cat > $TEST/difffiles/difffiles/difffiles/simple <<EOF
this is an old line

this is a local line
EOF
	cat > $OLD/difffiles/difffiles/difffiles/conflict <<EOF
this is an old file
EOF
	cat > $NEW/difffiles/difffiles/difffiles/conflict <<EOF
this is a new file
EOF
	cat > $TEST/difffiles/difffiles/difffiles/conflict <<EOF
this is a test file
EOF

	# /difffiles/difffiles/*: Cannot happen.  From the first three
	# tests, all three files are regular files.  The test file can
	# either be identical to the new file ('equal') or not
	# ('difffiles').

	## Tests for adding directories
	mkdir -p $OLD/adddir $NEW/adddir $TEST/adddir

	# /adddir/conflict: Add a new file in a directory that already
	# exists as a file.  This should generate two warnings.
	mkdir $NEW/adddir/conflict
	touch $NEW/adddir/conflict/newfile
	touch $TEST/adddir/conflict

	# /adddir/partial: Add a new file in a directory.  The
	# directory already exists in the test tree and contains a
	# different local file.  The new file from the new tree should
	# be added.
	for i in $NEW $TEST; do
		mkdir $i/adddir/partial
	done
	echo "foo" > $NEW/adddir/partial/file
	mkfifo $TEST/adddir/partial/fifo

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

	# /rmdir/partial: Remove a complete hierarchy when part of the
	# tree has already been removed locally.
	for i in $OLD $TEST; do
		mkdir -p $i/rmdir/partial/subdir
		mkfifo $i/rmdir/partial/subdir/fifo
	done
	echo "foo" > $OLD/rmdir/partial/subdir/file

	## Tests for converting files to directories and vice versa
	for i in $OLD $NEW $TEST; do
		for j in already old fromdir todir; do
			mkdir -p $i/dirchange/$j
		done
	done

	# /dirchange/already/fromdir: Convert a directory tree to a
	# file without conflicts where the test tree already has the
	# new file.  Nothing should happen.
	mkdir $OLD/dirchange/already/fromdir
	echo "blah" > $OLD/dirchange/already/fromdir/somefile
	for i in $NEW $TEST; do
		echo "bar" > $i/dirchange/already/fromdir
	done

	# /dirchange/already/todir: Convert an unmodified file to a
	# directory tree where the test tree already has the new
	# tree.  Nothing should happen.
	echo "baz" > $OLD/dirchange/already/todir
	for i in $NEW $TEST; do
		mkdir $i/dirchange/already/todir
		echo "blah" > $i/dirchange/already/todir/somefile
	done

	# /dirchange/old/fromdir: Convert a directory tree to a file.
	# The old files are unmodified and should be changed to the new tree.
	for i in $OLD $TEST; do
		mkdir $i/dirchange/old/fromdir
		echo "blah" > $i/dirchange/old/fromdir/somefile
	done
	echo "bar" > $NEW/dirchange/old/fromdir

	# /dirchange/old/todir: Convert a file to a directory tree.
	# The old file is unmodified and should be changed to the new
	# tree.
	for i in $OLD $TEST; do
		echo "foo" > $i/dirchange/old/todir
	done
	mkdir $NEW/dirchange/old/todir
	echo "bar" > $NEW/dirchange/old/todir/file

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

	## Tests for post-install actions

	# - Adding /etc/master.passwd should cause pwd_mkdb to be run
	echo "foo:*:16000:100::0:0:& user:/home/foo:/bin/tcsh" > \
	    $NEW/etc/master.passwd

	# - Verify that updating an unmodified /etc/login.conf builds
	# /etc/login.conf.db.
	cat > $OLD/etc/login.conf <<EOF
default:\\
	:passwd_format=md5:
EOF
	cat > $NEW/etc/login.conf <<EOF
default:\\
	:passwd_format=md5:\\
	:copyright=/etc/COPYRIGHT
EOF
	cp $OLD/etc/login.conf $TEST/etc/login.conf

	# - Verify that a merge without conflicts to /etc/mail/aliases
	# will trigger a newaliases run request.
	mkdir -p $OLD/etc/mail $NEW/etc/mail $TEST/etc/mail
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
postmaster: root
EOF

	# - Verify that updating an unmodified /etc/services builds
	# /var/db/services.db.
	cat > $OLD/etc/services <<EOF
rtmp		  1/ddp	   #Routing Table Maintenance Protocol
tcpmux		  1/tcp	   #TCP Port Service Multiplexer
tcpmux		  1/udp	   #TCP Port Service Multiplexer
EOF
	cat > $NEW/etc/services <<EOF
rtmp		  1/ddp	   #Routing Table Maintenance Protocol
tcpmux		  1/tcp	   #TCP Port Service Multiplexer
tcpmux		  1/udp	   #TCP Port Service Multiplexer
nbp		  2/ddp	   #Name Binding Protocol
compressnet	  2/tcp	   #Management Utility
compressnet	  2/udp	   #Management Utility
EOF
	cp $OLD/etc/services $TEST/etc/services
	mkdir -p $TEST/var/db
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

check_trees()
{

	echo "Checking tree for correct results:"

	## /equal/equal/equal:
	fifo /equal/equal/equal/fifo
	file /equal/equal/equal/file "foo"
	dir /equal/equal/equal/dir
	link /equal/equal/equal/link "bar"

	## /equal/first/first:
	missing /equal/first/first/fifo
	missing /equal/first/first/file
	missing /equal/first/first/dir
	missing /equal/first/first/link

	## /equal/difftype/difftype:
	file /equal/difftype/difftype/fifo "bar"
	link /equal/difftype/difftype/fromdir "test"

	## /equal/difflinks/difflinks:
	link /equal/difflinks/difflinks/link "bar"

	## /equal/difffiles/difffiles:
	file /equal/difffiles/difffiles/file "bar"

	## /first/equal/second:
	missing /first/equal/second/fifo
	missing /first/equal/second/file
	missing /first/equal/second/emptydir
	missing /first/equal/second/link
	missing /first/equal/second/fulldir

	## /first/first/equal:
	missing /first/first/equal/fifo
	missing /first/first/equal/file
	missing /first/first/equal/dir
	missing /first/first/equal/link

	## /first/difftype/second:
	present /first/difftype/second/fifo

	## /first/difflinks/second:
	link /first/difflinks/second/link "test link"

	## /first/difffiles/second:
	file /first/difffiles/second/file "bar"

	## /second/equal/first:
	file /second/equal/first/file "bar"
	fifo /second/equal/first/fifo
	link /second/equal/first/link "new"
	missing /second/equal/first/emptydir
	file /second/equal/first/fulldir/file "foo"

	## /second/second/equal:
	fifo /second/second/equal/fifo
	file /second/second/equal/file "foo"
	dir /second/second/equal/dir
	link /second/second/equal/link "bar"

	## /second/second/difftype:
	fifo /second/second/difftype/dir

	## /second/second/difflinks:
	link /second/second/difflinks/link "test link"

	## /second/second/difffiles:
	file /second/second/difffiles/file "test"
	conflict /second/second/difffiles/file 4f2ee8620a251fd53f06bb6112eb6ffa

	## /difftype/equal/difftype:
	link /difftype/equal/difftype/file "test"
	fifo /difftype/equal/difftype/fromdir
	missing /difftype/equal/difftype/todir

	## /difftype/first/first:
	missing /difftype/first/first/fifo

	## /difftype/difftype/equal:
	fifo /difftype/difftype/equal/fifo
	file /difftype/difftype/equal/file "bar"

	## /difftype/difftype/difftype:
	file /difftype/difftype/difftype/one "foo"
	link /difftype/difftype/difftype/two "bar"

	## /difftype/difftype/difflinks:
	link /difftype/difftype/difflinks/link "test"

	## /difftype/difftype/difffile:
	conflict /difftype/difftype/difffiles/file \
	    117f2bcd1f6491f6044e79e5a57a9229

	## /difflinks/equal/difflinks:
	link /difflinks/equal/difflinks/link "new"

	## /difflinks/first/first:
	missing /difflinks/first/first/link

	## /difflinks/difftype/difftype:
	file /difflinks/difftype/difftype/link "test"

	## /difflinks/difflinks/equal:
	link /difflinks/difflinks/equal/link "new"

	## /difflinks/difflinks/difflinks:
	link /difflinks/difflinks/difflinks/link "test"

	## /difffiles/equal/difffiles:
	file /difffiles/equal/difffiles/file "bar"

	## /difffiles/first/first:
	missing /difffiles/first/first/file

	## /difffiles/difftype/difftype:
	fifo /difffiles/difftype/difftype/file

	## /difffiles/difffiles/equal:
	file /difffiles/difffiles/equal/file "bar"

	## /difffiles/difffiles/difffiles:
	file /difffiles/difffiles/difffiles/simple "" \
	    cabc7e5e80b0946d79edd555e9648486
	file /difffiles/difffiles/difffiles/conflict "this is a test file"
	conflict /difffiles/difffiles/difffiles/conflict \
	    8261cfdd89280c4a6c26e4ac86541fe9

	## /adddir/conflict:
	file /adddir/conflict

	## /adddir/partial:
	file /adddir/partial/file "foo"
	fifo /adddir/partial/fifo

	## /rmdir/extra:
	dir /rmdir/extra
	file /rmdir/extra/localfile.txt "foo"

	## /rmdir/conflict:
	dir /rmdir/conflict/difftype
	present /rmdir/conflict

	## /rmdir/partial:
	missing /rmdir/partial

	## /dirchange/already/fromdir:
	file /dirchange/already/fromdir "bar"

	## /dirchange/already/todir:
	file /dirchange/already/todir/somefile "blah"

	## /dirchange/old/fromdir:
	file /dirchange/old/fromdir "bar"

	## /dirchange/old/todir
	file /dirchange/old/todir/file "bar"

	## /dirchange/fromdir/extradir:
	missing /dirchange/fromdir/extradir/file
	fifo /dirchange/fromdir/extradir/fifo

	## /dirchange/fromdir/conflict:
	file /dirchange/fromdir/conflict/somefile "bar"

	## /dirchange/todir/difffile:
	file /dirchange/todir/difffile "bar"

	## /dirchange/todir/difftype:
	fifo /dirchange/todir/difftype

	## Tests for post-install actions
	file /etc/master.passwd
	file /etc/passwd
	file /etc/pwd.db
	file /etc/spwd.db
	file /etc/login.conf "" 7774a0f9a3a372c7c109c32fd31c4b6b
	file /etc/login.conf.db
	file /etc/mail/aliases "" 7d598f89ec040ab56af54011bdb83337
	file /etc/services "" 37fb6a8d1273f3b78329d431f21d9c7d
	file /var/db/services.db
}

if [ `id -u` -ne 0 ]; then
	echo "must be root"
	exit 0
fi

if [ -r /etc/etcupdate.conf ]; then
	echo "WARNING: /etc/etcupdate.conf settings may break some tests."
fi

build_trees

$COMMAND -nr -d $WORKDIR -D $TEST > $WORKDIR/testn.out

cat > $WORKDIR/correct.out <<EOF
  D /dirchange/fromdir/extradir/file
  D /dirchange/old/fromdir/somefile
  D /first/equal/second/fifo
  D /first/equal/second/file
  D /first/equal/second/fulldir/file
  D /first/equal/second/link
  D /rmdir/partial/subdir/fifo
  D /rmdir/partial/subdir
  D /rmdir/partial
  D /first/equal/second/fulldir
  D /first/equal/second/emptydir
  C /difffiles/difffiles/difffiles/conflict
  M /difffiles/difffiles/difffiles/simple
  U /difffiles/equal/difffiles/file
  U /difflinks/equal/difflinks/link
  C /difftype/difftype/difffiles/file
  U /difftype/equal/difftype/file
  U /difftype/equal/difftype/fromdir
  D /difftype/equal/difftype/todir
  U /dirchange/old/fromdir
  U /dirchange/old/todir
  U /etc/login.conf
  M /etc/mail/aliases
  U /etc/services
  A /adddir/partial/file
  A /dirchange/old/todir/file
  A /etc/master.passwd
  A /second/equal/first/fifo
  A /second/equal/first/file
  A /second/equal/first/fulldir/file
  A /second/equal/first/link
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
  Needs update: /etc/mail/aliases.db (requires manual update via newaliases(1))
EOF

echo "Differences for -n:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/testn.out \
    || failed=YES

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

echo "Differences for real:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || failed=YES

check_trees

[ "${FAILED}" = no ]
