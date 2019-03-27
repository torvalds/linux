#!/bin/sh
# This file is in the public domain
# $FreeBSD$

set -e

OPLIST=`sh listallopts.sh`

ODIR=/usr/obj/`pwd`
RDIR=${ODIR}/_.result
export ODIR RDIR


compa ( ) (
	if [ ! -f $1/_.mtree ] ; then
		return
	fi
	if [ ! -f $2/_.mtree ] ; then
		return
	fi
	
	mtree -k uid,gid,mode,nlink,size,link,type,flags \
	    -f ${1}/_.mtree -f $2/_.mtree > $2/_.mtree.all.txt || true
	grep '^		' $2/_.mtree.all.txt > $4/$3.mtree.chg.txt || true
	grep '^[^	]' $2/_.mtree.all.txt > $4/$3.mtree.sub.txt || true
	grep '^	[^	]' $2/_.mtree.all.txt > $4/$3.mtree.add.txt || true
	a=`wc -l < $4/$3.mtree.add.txt`
	s=`wc -l < $4/$3.mtree.sub.txt`
	c=`wc -l < $4/$3.mtree.chg.txt`
	c=`expr $c / 2 || true`

	br=`awk 'NR == 2 {print $3}' $1/_.df`
	bt=`awk 'NR == 2 {print $3}' $2/_.df`
	echo $3 A $a S $s C $c B $bt D `expr $br - $bt`
)

for o in $OPLIST
do
	md=`echo "${o}=foo" | md5`
	m=${RDIR}/$md
	if [ ! -d $m ] ; then
		continue
	fi
	if [ ! -d $m/iw -a ! -d $m/bw -a ! -d $m/w ] ; then
		continue
	fi
	echo "=== reduce ${o}"

	echo
	echo -------------------------------------------------------------
	echo $md
	cat $m/src.conf
	echo -------------------------------------------------------------
	if [ ! -f $m/iw/done ] ; then
		echo "IW pending"
	elif [ ! -f $m/iw/_.success ] ; then
		echo "IW failed"
	fi
	if [ ! -f $m/bw/done ] ; then
		echo "BW pending"
	elif [ ! -f $m/bw/_.success ] ; then
		echo "BW failed"
	fi
	if [ ! -f $m/w/done ] ; then
		echo "W pending"
	elif [ ! -f $m/w/_.success ] ; then
		echo "W failed"
	fi
	(
	for x in iw bw w
	do
		compa ${RDIR}/Ref/ $m/$x r-$x $m
	done
	) > $m/stats
	cat $m/stats
done
echo "== reduce done"
