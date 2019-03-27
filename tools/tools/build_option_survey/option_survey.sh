#!/bin/sh
# This file is in the public domain
# $FreeBSD$

set -ex

OPLIST=`sh listallopts.sh`

MDUNIT=47
export MDUNIT

ODIR=/usr/obj/`pwd`
FDIR=${ODIR}/files
MNT=${ODIR}/_.mnt
RDIR=${ODIR}/_.result
: ${MAKE_JOBS:="-j$(sysctl -n hw.ncpu)"}

export ODIR MNT RDIR FDIR

bw ( ) (
	cd ../../.. 
	make showconfig \
		SRCCONF=${ODIR}/src.conf __MAKE_CONF=/dev/null \
		> ${FDIR}/_.sc 2>&1
	a=$?
	echo retval $a
	if [ $a -ne 0 ] ; then
		exit 1
	fi
	make ${MAKE_JOBS} buildworld \
		SRCCONF=${ODIR}/src.conf __MAKE_CONF=/dev/null \
		> ${FDIR}/_.bw 2>&1
	a=$?
	echo retval $a
	if [ $a -ne 0 ] ; then
		exit 1
	fi
	make ${MAKE_JOBS} buildkernel \
		KERNCONF=GENERIC \
		SRCCONF=${ODIR}/src.conf __MAKE_CONF=/dev/null \
		> ${FDIR}/_.bk 2>&1
	a=$?
	echo retval $a
	if [ $a -ne 0 ] ; then
		exit 1
	fi
	exit 0
)

iw ( ) (
	trap "umount ${MNT} || true" 1 2 15 EXIT
	newfs -O1 -U -b 4096 -f 512 /dev/md$MDUNIT
	mkdir -p ${MNT}
	mount /dev/md${MDUNIT} ${MNT}

	cd ../../..
	make ${MAKE_JOBS} installworld \
		SRCCONF=${ODIR}/src.conf __MAKE_CONF=/dev/null \
		DESTDIR=${MNT} \
		> ${FDIR}/_.iw 2>&1
	a=$?
	echo retval $a
	if [ $a -ne 0 ] ; then
		exit 1
	fi
	cd etc
	make distribution \
		SRCCONF=${ODIR}/src.conf __MAKE_CONF=/dev/null \
		DESTDIR=${MNT} \
		> ${FDIR}/_.etc 2>&1
	a=$?
	echo retval $a
	if [ $a -ne 0 ] ; then
		exit 1
	fi
	cd ..
	make ${MAKE_JOBS} installkernel \
		KERNCONF=GENERIC \
		DESTDIR=${MNT} \
		SRCCONF=${ODIR}/src.conf __MAKE_CONF=/dev/null \
		> ${FDIR}/_.ik 2>&1
	a=$?
	echo retval $a
	if [ $a -ne 0 ] ; then
		exit 1
	fi

	sync ${MNT}
	( cd ${MNT} && mtree -c ) > ${FDIR}/_.mtree
	( cd ${MNT} && du ) > ${FDIR}/_.du
	( df -i ${MNT} ) > ${FDIR}/_.df
	echo success > ${FDIR}/_.success
	sync
	sleep 1
	sync
	sleep 1
	trap "" 1 2 15 EXIT
	umount ${MNT}
	echo "iw done"
)


# Clean and recreate the ODIR

if true ; then 
	echo "=== Clean and recreate ${ODIR}"
	if rm -rf ${ODIR} ; then
		true
	else
		chflags -R noschg ${ODIR}
		rm -rf ${ODIR}
	fi
	mkdir -p ${ODIR} ${FDIR} ${MNT}

fi

trap "umount ${MNT} || true; mdconfig -d -u $MDUNIT" 1 2 15 EXIT

umount $MNT || true
mdconfig -d -u $MDUNIT || true
dd if=/dev/zero of=${ODIR}/imgfile bs=1m count=4096
mdconfig -a -t vnode -f ${ODIR}/imgfile -u $MDUNIT

# Build & install the reference world

if true ; then 
	echo "=== Build reference world"
	echo '' > ${ODIR}/src.conf
	MAKEOBJDIRPREFIX=$ODIR/_.ref 
	export MAKEOBJDIRPREFIX
	bw
	echo "=== Install reference world"
	mkdir -p ${RDIR}/Ref
	iw
	mv ${FDIR}/_.* ${RDIR}/Ref
fi

# Parse option list into subdirectories with src.conf files.

if true ; then
	rm -rf ${RDIR}/[0-9a-f]*
	for o in $OPLIST
	do
		echo "${o}=foo" > ${FDIR}/_src.conf
		m=`md5 < ${FDIR}/_src.conf`
		mkdir -p ${RDIR}/$m
		mv ${FDIR}/_src.conf ${RDIR}/$m/src.conf
	done
fi

# Run through each testtarget in turn

if true ; then
	for d in ${RDIR}/[0-9a-z]*
	do
		if [ ! -d $d ] ; then
			continue;
		fi
		echo '------------------------------------------------'
		cat $d/src.conf
		echo '------------------------------------------------'
		cp $d/src.conf ${ODIR}/src.conf

		if [ ! -f $d/iw/done ] ; then
			MAKEOBJDIRPREFIX=$ODIR/_.ref
			export MAKEOBJDIRPREFIX
			echo "# BW(ref)+IW(ref) `cat $d/src.conf`"
			rm -rf $d/iw
			mkdir -p $d/iw
			iw || true
			mv ${FDIR}/_.* $d/iw || true
			touch $d/iw/done
		fi
		if [ ! -f $d/bw/done ] ; then
			MAKEOBJDIRPREFIX=$ODIR/_.tst 
			export MAKEOBJDIRPREFIX
			echo "# BW(opt) `cat $d/src.conf`"
			rm -rf $d/w $d/bw
			mkdir -p $d/w $d/bw
			if bw ; then
				mv ${FDIR}/_.* $d/bw || true

				echo "# BW(opt)+IW(opt) `cat $d/src.conf`"
				iw || true
				mv ${FDIR}/_.* $d/w || true
				touch $d/w/done

				echo "# BW(opt)+IW(ref) `cat $d/src.conf`"
				echo '' > ${ODIR}/src.conf
				iw || true
				mv ${FDIR}/_.* $d/bw || true
				touch $d/bw/done
			else
				mv ${FDIR}/_.* $d/bw || true
				touch $d/bw/done $d/w/done
			fi
		fi
	done
fi
