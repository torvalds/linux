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

# Various regression tests to test the -F flag to the 'update' command.

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: fbsdid.sh [-s script] [-w workdir]"
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

# Store a FreeBSD ID string in a specified file.  The first argument
# is the file, the remaining arguments are the comment to use.
store_id()
{
	local file

	file=$1
	shift

	echo -n '# $FreeBSD' >> $file
	echo -n "$@" >> $file
	echo '$' >> $file
}

# These tests deal with FreeBSD ID string conflicts.  We run the test
# twice, once without -F and once with -F.
build_trees()
{
	local i

	rm -rf $OLD $NEW $TEST $CONFLICTS
	mkdir -p $OLD $NEW $TEST

	# remove: Remove a file where the only local difference is a
	# change in the FreeBSD ID string.
	store_id $OLD/remove
	store_id $TEST/remove ": head/remove 12345 jhb "

	# old: Modify a file where the only local difference between
	# the old and test files is a change in the FreeBSD ID string.
	store_id $OLD/old ": src/old,v 1.1 jhb Exp "
	store_id $NEW/old ": head/old 12345 jhb "
	store_id $TEST/old ": head/old 12000 jhb "
	for i in $OLD $TEST; do
		cat >> $i/old <<EOF

an old file
EOF
	done
	cat >> $NEW/old <<EOF

a new file
EOF

	# already: Modify a file where the local file already matches
	# the new file except for a change in the FreeBSD ID string.
	store_id $OLD/already ": src/already,v 1.1 jhb Exp "
	store_id $NEW/already ": head/already 12345 jhb "
	store_id $TEST/already ": src/already,v 1.2 jhb Exp "
	cat >> $OLD/already <<EOF

another old file
EOF
	for i in $NEW $TEST; do
		cat >> $i/already <<EOF

another new file
EOF
	done

	# add: Add a file that already exists where the only local
	# difference is a change in the FreeBSD ID string.
	store_id $NEW/add ": head/add 12345 jhb "
	store_id $TEST/add ""

	# conflict: Modify a file where the local file has a different
	# FreeBSD ID string.  This should still generate a conflict
	# even in the -F case.
	store_id $OLD/conflict ": head/conflict 12000 jhb "
	store_id $NEW/conflict ": head/conflict 12345 jhb "
	store_id $TEST/conflict ""
	cat >> $OLD/conflict <<EOF

this is the old file
EOF
	cat >> $NEW/conflict <<EOF

this is the new file
EOF
	cat >> $TEST/conflict <<EOF

this is the local file
EOF

	# local: A file with local modifications has a different
	# FreeBSD ID string and the only differences between the old
	# and new versions are a change in the FreeBSD ID string.
	# This will just update the FreeBSD ID string in the -F case.
	for i in $OLD $NEW $TEST; do
		cat >> $i/local <<EOF
# Some leading text
#
EOF
	done

	store_id $OLD/local ": head/local 12000 jhb "
	store_id $NEW/local ": head/local 12345 jhb "
	store_id $TEST/local ": src/local,v 1.5 jhb Exp "

	for i in $OLD $NEW $TEST; do
		cat >> $i/local <<EOF

this is a file
EOF
	done

	cat >> $TEST/local <<EOF

these are some local mods to the file
EOF

	# local-already: A file with local modifications has the same
	# FreeBSD ID string as the new version of the file and the
	# only differences between the old and new versions are a
	# change in the FreeBSD ID string.  Nothing should happen in
	# the -F case.
	store_id $OLD/local-already ": head/local 12000 jhb "
	for i in $NEW $TEST; do
		store_id $i/local-already ": head/local 12345 jhb "
	done

	for i in $OLD $NEW $TEST; do
		cat >> $i/local-already <<EOF

this is a file
EOF
	done

	cat >> $TEST/local-already <<EOF

these are some local mods to the file
EOF

	# local-remove: A file removed locally changed it's FreeBSD ID
	# but nothing else
	store_id $OLD/local-remove ": head/local-remove 12000 jhb "
	store_id $NEW/local-remove ": head/local-remove 12345 jhb "
	for i in $OLD $NEW; do
		cat >> $i/local-remove <<EOF

this is a file
EOF
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

# First run the test without -F.

build_trees

$COMMAND -r -d $WORKDIR -D $TEST > $WORKDIR/test.out

cat > $WORKDIR/correct.out <<EOF
  C /already
  C /conflict
  C /local
  M /local-already
  C /old
  C /add
Warnings:
  Modified regular file remains: /remove
  Removed file changed: /local-remove
EOF

echo "Differences for regular:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

file /remove "" 1bb4776213af107077be78fead8a351c
file /old "" 2f799a7addc4132563ef9b44adc66157
conflict /old 8441be64a1540f2ff584015279682425
file /already "" aa53bd506f65d01d766e7ba028585e1d
conflict /already f44105abb1fa3293e95c5d77e428d418
file /add "" 1dc8c617e541d1fd1b4c70212f71d8ae
conflict /add f99081e0da9a07f3cfebb430c0414941
file /conflict "" dc27978df125b0daeb7d9b93265f03fd
conflict /conflict 868452f666fea1c60ffb918ad9ad9607
file /local "" aa33e614b5e749449f230e2a2b0072eb
conflict /local 3df93e64043c8e348fc625b93ea220f4
file /local-already "" 0298b958a603049f45ae6a109c4f7fea
missing /local-remove

# Now test with -F.

build_trees

$COMMAND -rF -d $WORKDIR -D $TEST > $WORKDIR/testF.out

cat > $WORKDIR/correctF.out <<EOF
  D /remove
  U /already
  C /conflict
  M /local
  U /old
  U /add
EOF

echo "Differences for -F:"
diff -u -L "correct" $WORKDIR/correctF.out -L "test" $WORKDIR/testF.out \
    || FAILED=yes

missing /remove
file /old "" 6a9f34f109d94406a4de3bc5d72de259
noconflict /old
file /already "" 21f4eca3aacc702c49878c8da7afd3d0
noconflict /already
file /add "" 0208bd647111fedf6318511712ab9e97
noconflict /add
file /conflict "" dc27978df125b0daeb7d9b93265f03fd
conflict /conflict 868452f666fea1c60ffb918ad9ad9607
file /local "" 3ed5a35e380c8a93fb5f599d4c052713
file /local-already "" 0298b958a603049f45ae6a109c4f7fea
missing /local-remove

# Now test with -F and -A forcing all installs.  (-A should have
# precedence over -F)

build_trees

$COMMAND -A '/*' -rF -d $WORKDIR -D $TEST > $WORKDIR/testAF.out

cat > $WORKDIR/correctAF.out <<EOF
  D /remove
  U /already
  U /conflict
  U /local
  U /local-already
  A /local-remove
  U /old
  U /add
EOF

echo "Differences for -A '/*' -F:"
diff -u -L "correct" $WORKDIR/correctAF.out -L "test" $WORKDIR/testAF.out \
    || FAILED=yes

missing /remove
file /old "" 6a9f34f109d94406a4de3bc5d72de259
noconflict /old
file /already "" 21f4eca3aacc702c49878c8da7afd3d0
noconflict /already
file /add "" 0208bd647111fedf6318511712ab9e97
noconflict /add
file /conflict "" 75ee141c4136beaf14e39de92efa84e4
noconflict /conflict
file /local "" 6a8fc5c2755b7a49015089f5e1dbe092
file /local-already "" 49045f8b51542dd634655301cd296f66
file /local-remove "" 5c38322efed4014797d7127f5c652d9d

[ "${FAILED}" = no ]
