#!/bin/sh
# $FreeBSD$

set -e

make a
make b
foo() {
	f="${1}_${2}_${3}_${4}_${5}_${6}"
	echo $f
	sh ccd.sh $1 $2 $3 $4 $5 $6 > _.$f
	if [ -f ref.$f ] ; then
		diff -u -I '$FreeBSD' ref.$f _.$f
	fi
}

foo 128k 128k 128k 128k 0 0 
foo 128k 128k 128k 128k 0 4
foo 128k 128k 128k 128k 4 0 
foo 128k 128k 128k 128k 4 2
foo 128k 128k 128k 128k 4 4

foo 256k 128k 128k 128k 0 0 
foo 256k 128k 128k 128k 0 4
foo 256k 128k 128k 128k 4 0 
foo 256k 128k 128k 128k 4 2
foo 256k 128k 128k 128k 4 4

foo 256k 128k 384k 128k 0 0 
foo 256k 128k 384k 128k 0 4
foo 256k 128k 384k 128k 4 0 
foo 256k 128k 384k 128k 4 2
foo 256k 128k 384k 128k 4 4

foo 256k 128k 384k 128k 16 0 
foo 256k 128k 384k 128k 16 4
foo 256k 128k 384k 128k 16 0 
foo 256k 128k 384k 128k 16 2
foo 256k 128k 384k 128k 16 4
