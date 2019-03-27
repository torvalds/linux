#!/bin/sh
#
# Copyright (c) 1994-2009 Poul-Henning Kamp.
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
#

set -e

exec < /dev/null

if [ `uname -m` = "i386" -o `uname -m` = "amd64" ] ; then
	TARGET_PART=`df / | sed '
	1d
	s/[    ].*//
	s,/dev/,,
	s,s1a,s3a,
	s,s2a,s1a,
	s,s3a,s2a,
	'`

	FREEBSD_PART=`sed -n	\
		-e 's/#.*//'	\
		-e '/[ 	]\/freebsd[ 	]/!d'	\
		-e 's/[ 	].*//p'	\
		/etc/fstab`

	# Calculate a suggested gpart command
	TARGET_DISK=`expr ${TARGET_PART} : '\(.*\)s[12]a$' || true`
	TARGET_SLICE=`expr ${TARGET_PART} : '.*s\([12]\)a$' || true`
	GPART_SUGGESTION="gpart set -a active -i $TARGET_SLICE /dev/$TARGET_DISK"
	unset TARGET_DISK TARGET_SLICE
else
	TARGET_PART=unknown
	FREEBSD_PART=unknown
	GPART_SUGGESTION=unknown
fi


# Relative to /freebsd
PORTS_PATH=ports
SRC_PATH=src
# OBJ_PATH=obj

# Name of kernel
KERNCONF=GENERIC

# srcconf
#SRCCONF="SRCCONF=/usr/src/src.conf"

# -j arg to make(1)

ncpu=`sysctl -n kern.smp.cpus`
if [ $ncpu -gt 1 ] ; then
	JARG="-j $ncpu"
fi

# serial console ?
SERCONS=false

PKG_DIR=/usr/ports/packages/All

# Remotely mounted distfiles
# REMOTEDISTFILES=fs:/rdonly/distfiles

# Proxy
#FTP_PROXY=http://127.0.0.1:3128/
#HTTP_PROXY=http://127.0.0.1:3128/
#export FTP_PROXY HTTP_PROXY

PORTS_WE_WANT='
'

PORTS_OPTS="BATCH=YES A4=yes"

PORTS_WITHOUT=""
PORTS_WITH=""

CONFIGFILES='
'

SBMNT="/mnt.sysbuild"

cleanup() (
)

before_ports() (
)

before_ports_chroot() (
)

final_root() (
)

final_chroot() (
)

#######################################################################
# -P is a pretty neat way to clean junk out from your ports dist-files:
#
#	mkdir /freebsd/ports/distfiles.old
#	mv /freebsd/ports/distfiles/* /freebsd/ports/distfiles.old
#	sh sysbuild.sh -c $yourconfig -P /freebsd/ports/distfiles.old
#	rm -rf /freebsd/ports/distfiles.old
#
# Unfortunately bsd.ports.mk does not attempt to use a hard-link so
# while this runs you need diskspace for both your old and your "new"
# distfiles.
#
#######################################################################

usage () {
	(
        echo "Usage: $0 [-b/-k/-w] [-c config_file]"
        echo "  -b      suppress builds (both kernel and world)"
        echo "  -k      suppress buildkernel"
        echo "  -w      suppress buildworld"
        echo "  -p      used cached packages"
        echo "  -P <dir> prefetch ports"
        echo "  -c      specify config file"
        ) 1>&2
        exit 2
}

#######################################################################
#######################################################################

if [ ! -f $0 ] ; then
	echo "Must be able to access self ($0)" 1>&2
	exit 1
fi

if grep -q 'Magic String: 0`0nQT40W%l,CX&' $0 ; then
	true
else
	echo "self ($0) does not contain magic string" 1>&2
	exit 1
fi

#######################################################################

set -e

log_it() (
	a="$*"
	set `cat /tmp/_sb_log`
	TX=`date +%s`
	echo "$1 $TX" > /tmp/_sb_log
	DT=`expr $TX - $1 || true`
	DL=`expr $TX - $2 || true`
	echo -n "### `date +%H:%M:%S`"
	printf " ### %5d ### %5d ### %s\n" $DT $DL "$a"
)

#######################################################################

ports_make() {
	make $* WITH="${PORTS_WITH}" WITHOUT="${PORTS_WITHOUT}" ${PORTS_OPTS}
}

ports_recurse() (
	cd /usr/ports
	t=$1
	shift
	if [ "x$t" = "x." ] ; then
		true > /tmp/_.plist
		true > /tmp/_.plist.tdone
		echo 'digraph {' > /tmp/_.plist.dot
	fi
	if grep -q "^$t\$" /tmp/_.plist.tdone ; then
		return
	fi
	echo "$t" >> /tmp/_.plist.tdone
	for d
	do
		fl=""
		if [ ! -d $d ] ; then
			fl=FLAVOR=`expr $d : '.*@\(.*\)'`
			bd=`expr $d : '\(.*\)@.*'`
			if [ ! -d $bd ] ; then
				echo "Missing port $d ($t) (fl $fl) (bd $bd)" 1>&2
				continue
			fi
			# echo "Flavored port $d ($t) (fl $fl) (bd $bd)" 1>&2
			d=$bd
		fi
		d=`cd /usr/ports && cd $d && /bin/pwd`
		if [ ! -f $d/Makefile ] ; then
			echo "Missing port (Makefile) $d" 1>&2
			continue
		fi
		if [ "x$t" != "x." ] ; then
			echo "\"$t\" -> \"$d\"" >> /tmp/_.plist.dot
		fi
		if grep -q "^$d\$" /tmp/_.plist ; then
			true
		elif grep -q "^$d\$" /tmp/_.plist.tdone ; then
			true
		else
			(
			cd $d
			l=""
			for a in `ports_make -V _UNIFIED_DEPENDS $fl`
			do
				x=`expr "$a" : '.*:\(.*\)'`
				l="${l} ${x}"
			done
			ports_recurse $d $l
			)
			echo "$d" >> /tmp/_.plist
		fi
	done
	if [ "x$t" = "x." ] ; then
		echo '}' >> /tmp/_.plist.dot
	fi
)

ports_build() (

	ports_recurse . $PORTS_WE_WANT 

	if [ "x${PKG_DIR}" != "x" ] ; then
		mkdir -p ${PKG_DIR}
	fi

	pd=`cd /usr/ports && /bin/pwd`
	# Now build & install them
	for p in `cat /tmp/_.plist`
	do
		b=`echo $p | tr / _`
		t=`echo $p | sed "s,${pd},,"`
		pn=`cd $p && ports_make package-name`

		if [ "x`basename $p`" == "xpkg" ] ; then
			log_it "Very Special: $t ($pn)"

			(
			cd $p
			ports_make clean all install 
			) > _.$b 2>&1 < /dev/null
			continue
		fi

		if pkg info $pn > /dev/null 2>&1 ; then
			log_it "Already installed: $t ($pn)"
			continue
		fi

		if [ "x${PKG_DIR}" != "x" -a -f ${PKG_DIR}/$pn.txz ] ; then
			if [ "x$use_pkg" = "x-p" ] ; then
				log_it "Install $t ($pn)"
				(
				set +e
				pkg add ${PKG_DIR}/$pn.txz || true
				) > _.$b 2>&1 < /dev/null
				continue
			fi
		fi

		miss=`(cd $p ; ports_make missing) || true`

		if [ "x${miss}" != "x" ] ; then
			log_it "NB: MISSING for $p:" $miss
		fi

		log_it "build $pn ($p)"
		(
			set +e
			cd $p
			ports_make clean
			if ports_make install ; then
				if [ "x${PKG_DIR}" != "x" ] ; then
					ports_make package
				fi
			else
				log_it FAIL build $p
			fi
			ports_make clean

		) > _.$b 2>&1 < /dev/null
	done
)

ports_prefetch() (
	(
	set +x
	ldir=$1
	true > /${ldir}/_.prefetch
	echo "Building /tmp/_.plist" >> /${ldir}/_.prefetch

	ports_recurse . $PORTS_WE_WANT

	echo "Completed /tmp/_.plist" >> /${ldir}/_.prefetch
	# Now checksump/fetch them
	for p in `cat /tmp/_.plist`
	do
		b=`echo $p | tr / _`
		(
			cd $p
			if ports_make checksum ; then
				rm -f /${ldir}/_.prefetch.$b
				echo "OK $p" >> /${ldir}/_.prefetch
				exit 0
			fi
			ports_make distclean
			ports_make checksum || true

			if ports_make checksum > /dev/null 2>&1 ; then
				rm -f /${ldir}/_.prefetch.$b
				echo "OK $p" >> /${ldir}/_.prefetch
			else
				echo "BAD $p" >> /${ldir}/_.prefetch
			fi
		) > /${ldir}/_.prefetch.$b 2>&1
	done
	echo "Done" >> /${ldir}/_.prefetch
	) 
)

#######################################################################

do_world=true
do_kernel=true
do_prefetch=false
use_pkg=""
c_arg=""

set +e
args=`getopt bc:hkpP:w $*`
if [ $? -ne 0 ] ; then
	usage
fi
set -e

set -- $args
for i
do
	case "$i"
	in
	-b)
		shift;
		do_world=false
		do_kernel=false
		;;
	-c)
		c_arg=$2
		if [ ! -f "$c_arg" ] ; then
			echo "Cannot read $c_arg" 1>&2
			usage
		fi
		. "$2"
		shift
		shift
		;;
	-h)
		usage
		;;
	-k)
		shift;
		do_kernel=false
		;;
	-p)
		shift;
		use_pkg="-p"
		;;
	-P)
		shift;
		do_prefetch=true
		distfile_cache=$1
		shift;
		;;
	-w)
		shift;
		do_world=false
		;;
	--)
		shift
		break;
		;;
	esac
done

#######################################################################

if [ "x$1" = "xchroot_script" ] ; then
	set -e

	shift

	before_ports_chroot

	ports_build

	exit 0
fi

if [ "x$1" = "xfinal_chroot" ] ; then
	final_chroot
	exit 0
fi

if [ $# -gt 0 ] ; then
        echo "$0: Extraneous arguments supplied"
        usage
fi

#######################################################################

T0=`date +%s`
echo $T0 $T0 > /tmp/_sb_log

[ ! -d ${SBMNT} ] && mkdir -p ${SBMNT}

if $do_prefetch ; then
	rm -rf /tmp/sysbuild/ports
	mkdir -p /tmp/sysbuild/ports
	ln -s ${distfile_cache} /tmp/sysbuild/ports/distfiles
	export PORTS_OPTS=CD_MOUNTPTS=/tmp/sysbuild
	ports_prefetch /tmp 
	exit 0
fi

log_it Unmount everything
(
	( cleanup )
	umount /freebsd/distfiles || true
	umount ${SBMNT}/freebsd/distfiles || true
	umount ${FREEBSD_PART} || true
	umount ${SBMNT}/freebsd || true
	umount ${SBMNT}/dev || true
	umount ${SBMNT} || true
	umount /dev/${TARGET_PART} || true
) # > /dev/null 2>&1

log_it Prepare running image
mkdir -p /freebsd
mount ${FREEBSD_PART} /freebsd

#######################################################################

if [ ! -d /freebsd/${PORTS_PATH} ] ;  then
	echo PORTS_PATH does not exist 1>&2
	exit 1
fi

if [ ! -d /freebsd/${SRC_PATH} ] ;  then
	echo SRC_PATH does not exist 1>&2
	exit 1
fi

log_it TARGET_PART $TARGET_PART
sleep 5

rm -rf /usr/ports
ln -s /freebsd/${PORTS_PATH} /usr/ports

rm -rf /usr/src
ln -s /freebsd/${SRC_PATH} /usr/src

if $do_world ; then
	if [ "x${OBJ_PATH}" != "x" ] ; then
		rm -rf /usr/obj
		(cd /freebsd && mkdir -p ${OBJ_PATH} && ln -s ${OBJ_PATH} /usr/obj)
	else
		rm -rf /usr/obj
		mkdir -p /usr/obj
	fi
fi

#######################################################################

for i in ${PORTS_WE_WANT}
do
	(
	cd /usr/ports
	if [ ! -d $i ]  ; then
		echo "Port $i not found" 1>&2
		exit 2
	fi
	)
done

#export PORTS_WE_WANT
#export PORTS_OPTS

#######################################################################

log_it Prepare destination partition
newfs -t -E -O2 -U /dev/${TARGET_PART} > /dev/null
mount /dev/${TARGET_PART} ${SBMNT}
mkdir -p ${SBMNT}/dev
mount -t devfs devfs ${SBMNT}/dev

if [ "x${REMOTEDISTFILES}" != "x" ] ; then
	rm -rf /freebsd/${PORTS_PATH}/distfiles
	ln -s /freebsd/distfiles /freebsd/${PORTS_PATH}/distfiles
	mkdir -p /freebsd/distfiles
	mount  ${REMOTEDISTFILES} /freebsd/distfiles
fi

log_it copy ports config files
(cd / ; find var/db/ports -print | cpio -dumpv ${SBMNT} > /dev/null 2>&1)

log_it "Start prefetch of ports distfiles"
ports_prefetch ${SBMNT} &

if $do_world ; then
	(
	cd /usr/src
	log_it "Buildworld"
	make ${JARG} -s buildworld ${SRCCONF} > ${SBMNT}/_.bw 2>&1
	)
fi

if $do_kernel ; then
	(
	cd /usr/src
	log_it "Buildkernel"
	make ${JARG} -s buildkernel KERNCONF=$KERNCONF > ${SBMNT}/_.bk 2>&1
	)
fi


log_it Installworld
(cd /usr/src && make ${JARG} installworld DESTDIR=${SBMNT} ${SRCCONF} ) \
	> ${SBMNT}/_.iw 2>&1

log_it distribution
(cd /usr/src && make -m /usr/src/share/mk distribution DESTDIR=${SBMNT} ${SRCCONF} ) \
	> ${SBMNT}/_.dist 2>&1

log_it Installkernel
(cd /usr/src && make ${JARG} installkernel DESTDIR=${SBMNT} KERNCONF=$KERNCONF ) \
	> ${SBMNT}/_.ik 2>&1

if [ "x${OBJ_PATH}" != "x" ] ; then
	rmdir ${SBMNT}/usr/obj
	( cd /freebsd && mkdir -p ${OBJ_PATH} && ln -s ${OBJ_PATH} ${SBMNT}/usr/obj )
fi

log_it Wait for ports prefetch
log_it "(Tail ${SBMNT}/_.prefetch for progress)"
wait

log_it Move filesystems

if [ "x${REMOTEDISTFILES}" != "x" ] ; then
	umount /freebsd/distfiles
fi
umount ${FREEBSD_PART} || true
mkdir -p ${SBMNT}/freebsd
mount ${FREEBSD_PART} ${SBMNT}/freebsd
if [ "x${REMOTEDISTFILES}" != "x" ] ; then
	mount  ${REMOTEDISTFILES} ${SBMNT}/freebsd/distfiles
fi

rm -rf ${SBMNT}/usr/ports || true
ln -s /freebsd/${PORTS_PATH} ${SBMNT}/usr/ports

rm -rf ${SBMNT}/usr/src || true
ln -s /freebsd/${SRC_PATH} ${SBMNT}/usr/src

log_it Build and install ports

# Make sure fetching will work in the chroot
if [ -f /etc/resolv.conf ] ; then
	log_it copy resolv.conf
	cp /etc/resolv.conf ${SBMNT}/etc
	chflags schg ${SBMNT}/etc/resolv.conf
fi

if [ -f /etc/localtime ] ; then
	log_it copy localtime
	cp /etc/localtime ${SBMNT}/etc
fi

log_it ldconfig in chroot
chroot ${SBMNT} sh /etc/rc.d/ldconfig start

log_it before_ports
( 
	before_ports 
)

log_it fixing fstab
sed "/[ 	]\/[ 	]/s;^[^ 	]*[ 	];/dev/${TARGET_PART}	;" \
	/etc/fstab > ${SBMNT}/etc/fstab

log_it build ports

cp $0 ${SBMNT}/root
cp /tmp/_sb_log ${SBMNT}/tmp
b=`basename $0`
if [ "x$c_arg" != "x" ] ; then
	cp $c_arg ${SBMNT}/root
	chroot ${SBMNT} sh /root/$0 -c /root/`basename $c_arg` $use_pkg chroot_script 
else
	chroot ${SBMNT} sh /root/$0 $use_pkg chroot_script
fi
cp ${SBMNT}/tmp/_sb_log /tmp

log_it create all mountpoints
grep -v '^[ 	]*#' ${SBMNT}/etc/fstab | 
while read a b c
do
	mkdir -p ${SBMNT}/$b
done

if [ "x$SERCONS" != "xfalse" ] ; then
	log_it serial console
	echo " -h" > ${SBMNT}/boot.config
	sed -i "" -e /ttyd0/s/off/on/ ${SBMNT}/etc/ttys
	sed -i "" -e /ttyu0/s/off/on/ ${SBMNT}/etc/ttys
	sed -i "" -e '/^ttyv[0-8]/s/	on/	off/' ${SBMNT}/etc/ttys
fi

log_it move dist config files "(expect warnings)"
(
	cd ${SBMNT}
	mkdir root/configfiles_dist
	find ${CONFIGFILES} -print | cpio -dumpv root/configfiles_dist
)

log_it copy live config files
(cd / && find ${CONFIGFILES} -print | cpio -dumpv ${SBMNT})

log_it final_root
( final_root )
log_it final_chroot
cp /tmp/_sb_log ${SBMNT}/tmp
if [ "x$c_arg" != "x" ] ; then
	chroot ${SBMNT} sh /root/$0 -c /root/`basename $c_arg` final_chroot
else
	chroot ${SBMNT} sh /root/$0 final_chroot
fi
cp ${SBMNT}/tmp/_sb_log /tmp
log_it "Check these messages (if any):"
grep '^Stop' ${SBMNT}/_* || true
log_it DONE
echo "Now you probably want to:"
echo "    $GPART_SUGGESTION"
echo "    shutdown -r now"
