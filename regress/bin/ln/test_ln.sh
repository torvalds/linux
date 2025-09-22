#!/bin/sh
#
#	$OpenBSD: test_ln.sh,v 1.2 2020/12/18 18:05:29 bluhm Exp $

set -f

get_dev_ino()
{
	stat -f %d:%i "$@"
}

compare_dirents()
{
	opt=
	if [ $# -eq 3 ]
	then
		opt=$1
		shift
	fi

	echo Comparing $1 and $2
	if [ `get_dev_ino $opt $1` != `get_dev_ino $opt $2` ]
	then
		echo comparison failed: $1 different than $2
		exit 1
	fi
}

test_ln()
{
	[ -e $2 ] || ln $1 $2
	compare_dirents $1 $2
}

test_ln_s()
{
	[ -h $2 ] || ln -s $1 $2
	compare_dirents -L $3 $2
}

test_ln_L()
{
	[ -e $2 ] || ln -L $1 $2

	# Need 3rd argument because $2 follows symlink $1
	compare_dirents $2 $3
}

test_ln_P()
{
	[ -e $2 ] || ln -P $1 $2
	compare_dirents $1 $2
}

test_ln   ./links/source ./links/hardlink1
test_ln_s source ./links/symlink1 ./links/source
test_ln_L ./links/symlink1 ./links/hardlink2 ./links/source
test_ln_P ./links/symlink1 ./links/symlink2
test_ln_s symlink1 ./links/symlink3 ./links/symlink1
test_ln_L ./links/symlink3 ./links/hardlink3 ./links/source
err=`LC_ALL=C ln -P ./links/symlink1 ./links/symlink2 2>&1`
if [ $? -eq 0 ]; then
	exit 1
fi
case $err in
 *"are identical"*"nothing done"*) ;;
 *) exit 1;;
esac
