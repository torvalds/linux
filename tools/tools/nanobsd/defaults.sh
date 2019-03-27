#!/bin/sh
#
# Copyright (c) 2005 Poul-Henning Kamp.
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

#######################################################################
#
# Setup default values for all controlling variables.
# These values can be overridden from the config file(s)
#
#######################################################################

# Name of this NanoBSD build.  (Used to construct workdir names)
NANO_NAME=full

# Source tree directory
NANO_SRC=/usr/src

# Where nanobsd additional files live under the source tree
NANO_TOOLS=tools/tools/nanobsd

# Where cust_pkgng() finds packages to install
NANO_PACKAGE_DIR=${NANO_SRC}/${NANO_TOOLS}/Pkg
NANO_PACKAGE_LIST="*"

# where package metadata gets placed
NANO_PKG_META_BASE=/var/db

# Path to mtree file to apply to anything copied by cust_install_files().
# If you specify this, the mtree file *must* have an entry for every file and
# directory located in Files.
#NANO_CUST_FILES_MTREE=""

# Object tree directory
# default is subdir of /usr/obj
#NANO_OBJ=""

# The directory to put the final images
# default is ${NANO_OBJ}
#NANO_DISKIMGDIR=""

# Make & parallel Make
NANO_MAKE="make"
NANO_PMAKE="make -j 3"

# The default name for any image we create.
NANO_IMGNAME="_.disk.full"
NANO_IMG1NAME="_.disk.image"

# Options to put in make.conf during buildworld only
CONF_BUILD=' '

# Options to put in make.conf during installworld only
CONF_INSTALL=' '

# Options to put in make.conf during both build- & installworld.
CONF_WORLD=' '

# Kernel config file to use
NANO_KERNEL=GENERIC

# Kernel modules to install. If empty, no modules are installed.
# Use "default" to install all built modules.
NANO_MODULES=

# Early customize commands.
NANO_EARLY_CUSTOMIZE=""

# Customize commands.
NANO_CUSTOMIZE=""

# Late customize commands.
NANO_LATE_CUSTOMIZE=""

# Newfs parameters to use
NANO_NEWFS="-b 4096 -f 512 -i 8192 -U"

# The drive name of the media at runtime
NANO_DRIVE=ada0

# Target media size in 512 bytes sectors
NANO_MEDIASIZE=2000000

# Number of code images on media (1 or 2)
NANO_IMAGES=2

# 0 -> Leave second image all zeroes so it compresses better.
# 1 -> Initialize second image with a copy of the first
NANO_INIT_IMG2=1

# Size of code file system in 512 bytes sectors
# If zero, size will be as large as possible.
NANO_CODESIZE=0

# Size of configuration file system in 512 bytes sectors
# Cannot be zero.
NANO_CONFSIZE=2048

# Size of data file system in 512 bytes sectors
# If zero: no partition configured.
# If negative: max size possible
NANO_DATASIZE=0

# Size of the /etc ramdisk in 512 bytes sectors
NANO_RAM_ETCSIZE=10240

# Size of the /tmp+/var ramdisk in 512 bytes sectors
NANO_RAM_TMPVARSIZE=10240

# boot0 flags/options and configuration
NANO_BOOT0CFG="-o packet -s 1 -m 3"
NANO_BOOTLOADER="boot/boot0sio"

# boot2 flags/options
# default force serial console
NANO_BOOT2CFG="-h -S115200"

# Backing type of md(4) device
# Can be "file" or "swap"
NANO_MD_BACKING="file"

# for swap type md(4) backing, write out the mbr only
NANO_IMAGE_MBRONLY=true

# Progress Print level
PPLEVEL=3

# Set NANO_LABEL to non-blank to form the basis for using /dev/ufs/label
# in preference to /dev/${NANO_DRIVE}
# Root partition will be ${NANO_LABEL}s{1,2}
# /cfg partition will be ${NANO_LABEL}s3
# /data partition will be ${NANO_LABEL}s4
NANO_LABEL=""
NANO_SLICE_ROOT=s1
NANO_SLICE_ALTROOT=s2
NANO_SLICE_CFG=s3
NANO_SLICE_DATA=s4
NANO_ROOT=s1a
NANO_ALTROOT=s2a

# Default ownwership for nopriv build
NANO_DEF_UNAME=root
NANO_DEF_GNAME=wheel

#######################################################################
# Architecture to build.  Corresponds to TARGET_ARCH in a buildworld.
# Unfortunately, there's no way to set TARGET at this time, and it
# conflates the two, so architectures where TARGET != TARGET_ARCH and
# TARGET can't be guessed from TARGET_ARCH do not work.  This defaults
# to the arch of the current machine.
NANO_ARCH=`uname -p`

# CPUTYPE defaults to "" which is the default when CPUTYPE isn't
# defined.
NANO_CPUTYPE=""

# Directory to populate /cfg from
NANO_CFGDIR=""

# Directory to populate /data from
NANO_DATADIR=""

# We don't need SRCCONF or SRC_ENV_CONF. NanoBSD puts everything we
# need for the build in files included with __MAKE_CONF. Override in your
# config file if you really must. We set them unconditionally here, though
# in case they are stray in the build environment
SRCCONF=/dev/null
SRC_ENV_CONF=/dev/null

#######################################################################
#
# The functions which do the real work.
# Can be overridden from the config file(s)
#
#######################################################################

# Export values into the shell. Must use { } instead of ( ) like
# other functions to avoid a subshell.
# We set __MAKE_CONF as a global since it is easier to get quoting
# right for paths with spaces in them.
make_export ( ) {
	# Similar to export_var, except puts the data out to stdout
	var=$1
	eval val=\$$var
	echo "Setting variable: $var=\"$val\""
	export $1
}

nano_make_build_env ( ) {
	__MAKE_CONF="${NANO_MAKE_CONF_BUILD}"
	make_export __MAKE_CONF
}

nano_make_install_env ( ) {
	__MAKE_CONF="${NANO_MAKE_CONF_INSTALL}"
	make_export __MAKE_CONF
}

# Extra environment variables for kernel builds
nano_make_kernel_env ( ) {
	if [ -f "${NANO_KERNEL}" ] ; then
		KERNCONFDIR="$(realpath $(dirname ${NANO_KERNEL}))"
		KERNCONF="$(basename ${NANO_KERNEL})"
		make_export KERNCONFDIR
		make_export KERNCONF
	else
		export KERNCONF="${NANO_KERNEL}"
		make_export KERNCONF
	fi
}

nano_global_make_env ( ) (
	# global settings for the make.conf file, if set
	[ -z "${NANO_ARCH}" ] || echo TARGET_ARCH="${NANO_ARCH}"
	[ -z "${NANO_CPUTYPE}" ] || echo TARGET_CPUTYPE="${NANO_CPUTYPE}"
)

# rm doesn't know -x prior to FreeBSD 10, so cope with a variety of build
# hosts for now. This will go away when support in the base goes away.
rm ( ) {
    echo "NANO RM $*"
	case $(uname -r) in
	7*|8*|9*) command rm $* ;;
	*) command rm -x $* ;;
	esac
}

#
# Create empty files in the target tree, and record the fact.  All paths
# are relative to NANO_WORLDDIR.
#
tgt_touch ( ) (
	cd "${NANO_WORLDDIR}"
	for i; do
		touch $i
		echo "./${i} type=file" >> ${NANO_METALOG}
	done
)

#
# Convert a directory into a symlink. Takes two arguments, the
# current directory and what it should become a symlink to. The
# directory is removed and a symlink is created. If we're doing
# a nopriv build, then append this fact to the metalog
#
tgt_dir2symlink ( ) (
	dir=$1
	symlink=$2

	cd "${NANO_WORLDDIR}"
	rm -rf "$dir"
	ln -s "$symlink" "$dir"
	if [ -n "$NANO_METALOG" ]; then
		echo "./${dir} type=link mode=0777 link=${symlink}" >> ${NANO_METALOG}
	fi
)

# run in the world chroot, errors fatal
CR ( ) {
	chroot "${NANO_WORLDDIR}" /bin/sh -exc "$*"
}

# run in the world chroot, errors not fatal
CR0 ( ) {
	chroot "${NANO_WORLDDIR}" /bin/sh -c "$*" || true
}

clean_build ( ) (
	pprint 2 "Clean and create object directory (${MAKEOBJDIRPREFIX})"

	if ! rm -rf ${MAKEOBJDIRPREFIX}/ > /dev/null 2>&1 ; then
		chflags -R noschg ${MAKEOBJDIRPREFIX}/
		rm -r ${MAKEOBJDIRPREFIX}/
	fi
)

make_conf_build ( ) (
	pprint 2 "Construct build make.conf ($NANO_MAKE_CONF_BUILD)"

	mkdir -p ${MAKEOBJDIRPREFIX}
	printenv > ${MAKEOBJDIRPREFIX}/_.env

	# Make sure we get all the global settings that NanoBSD wants
	# in addition to the user's global settings
	(
	nano_global_make_env
	echo "${CONF_WORLD}"
	echo "${CONF_BUILD}"
	) > ${NANO_MAKE_CONF_BUILD}
)

build_world ( ) (
	pprint 2 "run buildworld"
	pprint 3 "log: ${MAKEOBJDIRPREFIX}/_.bw"

	(
	nano_make_build_env
	set -o xtrace
	cd "${NANO_SRC}"
	${NANO_PMAKE} buildworld
	) > ${MAKEOBJDIRPREFIX}/_.bw 2>&1
)

build_kernel ( ) (
	pprint 2 "build kernel ($NANO_KERNEL)"
	pprint 3 "log: ${MAKEOBJDIRPREFIX}/_.bk"

	(
	nano_make_build_env
	nano_make_kernel_env

	# Note: We intentionally build all modules, not only the ones in
	# NANO_MODULES so the built world can be reused by multiple images.
	# Although MODULES_OVERRIDE can be defined in the kernel config
	# file to override this behavior. Just set NANO_MODULES=default.
	set -o xtrace
	cd "${NANO_SRC}"
	${NANO_PMAKE} buildkernel
	) > ${MAKEOBJDIRPREFIX}/_.bk 2>&1
)

clean_world ( ) (
	if [ "${NANO_OBJ}" != "${MAKEOBJDIRPREFIX}" ]; then
		pprint 2 "Clean and create object directory (${NANO_OBJ})"
		if ! rm -rf ${NANO_OBJ}/ > /dev/null 2>&1 ; then
			chflags -R noschg ${NANO_OBJ}
			rm -r ${NANO_OBJ}/
		fi
		mkdir -p "${NANO_OBJ}" "${NANO_WORLDDIR}"
		printenv > ${NANO_LOG}/_.env
	else
		pprint 2 "Clean and create world directory (${NANO_WORLDDIR})"
		if ! rm -rf "${NANO_WORLDDIR}/" > /dev/null 2>&1 ; then
			chflags -R noschg "${NANO_WORLDDIR}"
			rm -rf "${NANO_WORLDDIR}/"
		fi
		mkdir -p "${NANO_WORLDDIR}"
	fi
)

make_conf_install ( ) (
	pprint 2 "Construct install make.conf ($NANO_MAKE_CONF_INSTALL)"

	# Make sure we get all the global settings that NanoBSD wants
	# in addition to the user's global settings
	(
	nano_global_make_env
	echo "${CONF_WORLD}"
	echo "${CONF_INSTALL}"
	if [ -n "${NANO_NOPRIV_BUILD}" ]; then
	    echo NO_ROOT=t
	    echo METALOG=${NANO_METALOG}
	fi
	) >  ${NANO_MAKE_CONF_INSTALL}
)

install_world ( ) (
	pprint 2 "installworld"
	pprint 3 "log: ${NANO_LOG}/_.iw"

	(
	nano_make_install_env
	set -o xtrace
	cd "${NANO_SRC}"
	${NANO_MAKE} installworld DESTDIR="${NANO_WORLDDIR}" DB_FROM_SRC=yes
	chflags -R noschg "${NANO_WORLDDIR}"
	) > ${NANO_LOG}/_.iw 2>&1
)

install_etc ( ) (
	pprint 2 "install /etc"
	pprint 3 "log: ${NANO_LOG}/_.etc"

	(
	nano_make_install_env
	set -o xtrace
	cd "${NANO_SRC}"
	${NANO_MAKE} distribution DESTDIR="${NANO_WORLDDIR}" DB_FROM_SRC=yes
	# make.conf doesn't get created by default, but some ports need it
	# so they can spam it.
	cp /dev/null "${NANO_WORLDDIR}"/etc/make.conf
	) > ${NANO_LOG}/_.etc 2>&1
)

install_kernel ( ) (
	pprint 2 "install kernel ($NANO_KERNEL)"
	pprint 3 "log: ${NANO_LOG}/_.ik"

	(

	nano_make_install_env
	nano_make_kernel_env

	if [ "${NANO_MODULES}" != "default" ]; then
		MODULES_OVERRIDE="${NANO_MODULES}"
		make_export MODULES_OVERRIDE
	fi

	set -o xtrace
	cd "${NANO_SRC}"
	${NANO_MAKE} installkernel DESTDIR="${NANO_WORLDDIR}" DB_FROM_SRC=yes

	) > ${NANO_LOG}/_.ik 2>&1
)

native_xtools ( ) (
	print 2 "Installing the optimized native build tools for cross env"
	pprint 3 "log: ${NANO_LOG}/_.native_xtools"

	(

	nano_make_install_env
	set -o xtrace
	cd "${NANO_SRC}"
	${NANO_MAKE} native-xtools
	${NANO_MAKE} native-xtools-install DESTDIR="${NANO_WORLDDIR}"

	) > ${NANO_LOG}/_.native_xtools 2>&1
)

#
# Run the requested set of early customization scripts, run before
# buildworld.
#
run_early_customize ( ) {
	pprint 2 "run early customize scripts"
	for c in $NANO_EARLY_CUSTOMIZE
	do
		pprint 2 "early customize \"$c\""
		pprint 3 "log: ${NANO_LOG}/_.early_cust.$c"
		pprint 4 "`type $c`"
		{ set -x ; $c ; set +x ; } >${NANO_LOG}/_.early_cust.$c 2>&1
	done
}

#
# Run the requested set of customization scripts, run after we've
# done an installworld, installed the etc files, installed the kernel
# and tweaked them in the standard way.
#
run_customize ( ) (

	pprint 2 "run customize scripts"
	for c in $NANO_CUSTOMIZE
	do
		pprint 2 "customize \"$c\""
		pprint 3 "log: ${NANO_LOG}/_.cust.$c"
		pprint 4 "`type $c`"
		( set -x ; $c ) > ${NANO_LOG}/_.cust.$c 2>&1
	done
)

#
# Run any last-minute customization commands after we've had a chance to
# setup nanobsd, prune empty dirs from /usr, etc
#
run_late_customize ( ) (
	pprint 2 "run late customize scripts"
	for c in $NANO_LATE_CUSTOMIZE
	do
		pprint 2 "late customize \"$c\""
		pprint 3 "log: ${NANO_LOG}/_.late_cust.$c"
		pprint 4 "`type $c`"
		( set -x ; $c ) > ${NANO_LOG}/_.late_cust.$c 2>&1
	done
)

#
# Hook called after we run all the late customize commands, but
# before we invoke the disk imager. The nopriv build uses it to
# read in the meta log, apply the changes other parts of nanobsd
# have been recording their actions. It's not anticipated that
# a user's cfg file would override this.
#
fixup_before_diskimage ( ) (
	# Run the deduplication script that takes the matalog journal and
	# combines multiple entries for the same file (see source for
	# details). We take the extra step of removing the size keywords. This
	# script, and many of the user scripts, copies, appeneds and otherwise
	# modifies files in the build, changing their sizes.  These actions are
	# impossible to trap, so go ahead remove the size= keyword. For this
	# narrow use, it doesn't buy us any protection and just gets in the way.
	# The dedup tool's output must be sorted due to limitations in awk.
	if [ -n "${NANO_METALOG}" ]; then
		pprint 2 "Fixing metalog"
		cp ${NANO_METALOG} ${NANO_METALOG}.pre
		echo "/set uname=${NANO_DEF_UNAME} gname=${NANO_DEF_GNAME}" > ${NANO_METALOG}
		cat ${NANO_METALOG}.pre | ${NANO_TOOLS}/mtree-dedup.awk | \
		    sed -e 's/ size=[0-9][0-9]*//' | sort >> ${NANO_METALOG}
	fi
)

setup_nanobsd ( ) (
	pprint 2 "configure nanobsd setup"
	pprint 3 "log: ${NANO_LOG}/_.dl"

	(
	cd "${NANO_WORLDDIR}"

	# Move /usr/local/etc to /etc/local so that the /cfg stuff
	# can stomp on it.  Otherwise packages like ipsec-tools which
	# have hardcoded paths under ${prefix}/etc are not tweakable.
	if [ -d usr/local/etc ] ; then
		(
		cd usr/local/etc
		find . -print | cpio -dumpl ../../../etc/local
		cd ..
		rm -rf etc
		)
	fi

	# Always setup the usr/local/etc -> etc/local symlink.
	# usr/local/etc gets created by packages, but if no packages
	# are installed by this point, but are later in the process,
	# the symlink not being here causes problems. It never hurts
	# to have the symlink in error though.
	ln -s ../../etc/local usr/local/etc

	for d in var etc
	do
		# link /$d under /conf
		# we use hard links so we have them both places.
		# the files in /$d will be hidden by the mount.
		mkdir -p conf/base/$d conf/default/$d
		find $d -print | cpio -dumpl conf/base/
	done

	echo "$NANO_RAM_ETCSIZE" > conf/base/etc/md_size
	echo "$NANO_RAM_TMPVARSIZE" > conf/base/var/md_size

	# pick up config files from the special partition
	echo "mount -o ro /dev/${NANO_DRIVE}${NANO_SLICE_CFG}" > conf/default/etc/remount

	# Put /tmp on the /var ramdisk (could be symlink already)
	tgt_dir2symlink tmp var/tmp

	) > ${NANO_LOG}/_.dl 2>&1
)

setup_nanobsd_etc ( ) (
	pprint 2 "configure nanobsd /etc"

	(
	cd "${NANO_WORLDDIR}"

	# create diskless marker file
	touch etc/diskless

	[ -n "${NANO_NOPRIV_BUILD}" ] && chmod 666 etc/defaults/rc.conf

	# Make root filesystem R/O by default
	echo "root_rw_mount=NO" >> etc/defaults/rc.conf
	# Disable entropy file, since / is read-only /var/db/entropy should be enough?
	echo "entropy_file=NO" >> etc/defaults/rc.conf

	[ -n "${NANO_NOPRIV_BUILD}" ] && chmod 444 etc/defaults/rc.conf

	# save config file for scripts
	echo "NANO_DRIVE=${NANO_DRIVE}" > etc/nanobsd.conf

	echo "/dev/${NANO_DRIVE}${NANO_ROOT} / ufs ro 1 1" > etc/fstab
	echo "/dev/${NANO_DRIVE}${NANO_SLICE_CFG} /cfg ufs rw,noauto 2 2" >> etc/fstab
	mkdir -p cfg

	# Create directory for eventual /usr/local/etc contents
	mkdir -p etc/local
	)
)

prune_usr ( ) (
	# Remove all empty directories in /usr
	find "${NANO_WORLDDIR}"/usr -type d -depth -print |
		while read d
		do
			rmdir $d > /dev/null 2>&1 || true
		done
)

newfs_part ( ) (
	local dev mnt lbl
	dev=$1
	mnt=$2
	lbl=$3
	echo newfs ${NANO_NEWFS} ${NANO_LABEL:+-L${NANO_LABEL}${lbl}} ${dev}
	newfs ${NANO_NEWFS} ${NANO_LABEL:+-L${NANO_LABEL}${lbl}} ${dev}
	mount -o async ${dev} ${mnt}
)

# Convenient spot to work around any umount issues that your build environment
# hits by overriding this method.
nano_umount ( ) (
	umount ${1}
)

populate_slice ( ) (
	local dev dir mnt lbl
	dev=$1
	dir=$2
	mnt=$3
	lbl=$4
	echo "Creating ${dev} (mounting on ${mnt})"
	newfs_part ${dev} ${mnt} ${lbl}
	if [ -n "${dir}" -a -d "${dir}" ]; then
		echo "Populating ${lbl} from ${dir}"
		cd "${dir}"
		find . -print | grep -Ev '/(CVS|\.svn|\.hg|\.git)/' | cpio -dumpv ${mnt}
	fi
	df -i ${mnt}
	nano_umount ${mnt}
)

populate_cfg_slice ( ) (
	populate_slice "$1" "$2" "$3" "$4"
)

populate_data_slice ( ) (
	populate_slice "$1" "$2" "$3" "$4"
)

last_orders ( ) (
	# Redefine this function with any last orders you may have
	# after the build completed, for instance to copy the finished
	# image to a more convenient place:
	# cp ${NANO_DISKIMGDIR}/${NANO_IMG1NAME} /home/ftp/pub/nanobsd.disk
	true
)

#######################################################################
#
# Optional convenience functions.
#
#######################################################################

#######################################################################
# Common Flash device geometries
#

FlashDevice ( ) {
	if [ -d ${NANO_TOOLS} ] ; then
		. ${NANO_TOOLS}/FlashDevice.sub
	else
		. ${NANO_SRC}/${NANO_TOOLS}/FlashDevice.sub
	fi
	sub_FlashDevice $1 $2
}

#######################################################################
# USB device geometries
#
# Usage:
#	UsbDevice Generic 1000	# a generic flash key sold as having 1GB
#
# This function will set NANO_MEDIASIZE, NANO_HEADS and NANO_SECTS for you.
#
# Note that the capacity of a flash key is usually advertised in MB or
# GB, *not* MiB/GiB. As such, the precise number of cylinders available
# for C/H/S geometry may vary depending on the actual flash geometry.
#
# The following generic device layouts are understood:
#  generic           An alias for generic-hdd.
#  generic-hdd       255H 63S/T xxxxC with no MBR restrictions.
#  generic-fdd       64H 32S/T xxxxC with no MBR restrictions.
#
# The generic-hdd device is preferred for flash devices larger than 1GB.
#

UsbDevice ( ) {
	a1=`echo $1 | tr '[:upper:]' '[:lower:]'`
	case $a1 in
	generic-fdd)
		NANO_HEADS=64
		NANO_SECTS=32
		NANO_MEDIASIZE=$(( $2 * 1000 * 1000 / 512 ))
		;;
	generic|generic-hdd)
		NANO_HEADS=255
		NANO_SECTS=63
		NANO_MEDIASIZE=$(( $2 * 1000 * 1000 / 512 ))
		;;
	*)
		echo "Unknown USB flash device"
		exit 2
		;;
	esac
}

#######################################################################
# Setup serial console

cust_comconsole ( ) (
	# Enable getty on console
	sed -i "" -e /tty[du]0/s/off/on/ ${NANO_WORLDDIR}/etc/ttys

	# Disable getty on syscons devices
	sed -i "" -e '/^ttyv[0-8]/s/	on/	off/' ${NANO_WORLDDIR}/etc/ttys

	# Tell loader to use serial console early.
	echo "${NANO_BOOT2CFG}" > ${NANO_WORLDDIR}/boot.config
)

#######################################################################
# Allow root login via ssh

cust_allow_ssh_root ( ) (
	sed -i "" -e '/PermitRootLogin/s/.*/PermitRootLogin yes/' \
	    ${NANO_WORLDDIR}/etc/ssh/sshd_config
)

#######################################################################
# Install the stuff under ./Files

cust_install_files ( ) (
	cd "${NANO_TOOLS}/Files"
	find . -print | grep -Ev '/(CVS|\.svn|\.hg|\.git)/' | cpio -Ldumpv ${NANO_WORLDDIR}

	if [ -n "${NANO_CUST_FILES_MTREE}" -a -f ${NANO_CUST_FILES_MTREE} ]; then
		CR "mtree -eiU -p /" <${NANO_CUST_FILES_MTREE}
	fi
)

#######################################################################
# Install packages from ${NANO_PACKAGE_DIR}

cust_pkgng ( ) (
	mkdir -p ${NANO_WORLDDIR}/usr/local/etc
	local PKG_CONF="${NANO_WORLDDIR}/usr/local/etc/pkg.conf"
	local PKGCMD="env BATCH=YES ASSUME_ALWAYS_YES=YES PKG_DBDIR=${NANO_PKG_META_BASE}/pkg SIGNATURE_TYPE=none /usr/sbin/pkg"

	# Ensure pkg.conf points pkg to where the package meta data lives.
	touch ${PKG_CONF}
	if grep -Eiq '^PKG_DBDIR:.*' ${PKG_CONF}; then
		sed -i -e "\|^PKG_DBDIR:.*|Is||PKG_DBDIR: "\"${NANO_PKG_META_BASE}/pkg\""|" ${PKG_CONF}
	else
		echo "PKG_DBDIR: \"${NANO_PKG_META_BASE}/pkg\"" >> ${PKG_CONF}
	fi

	# If the package directory doesn't exist, we're done.
	if [ ! -d ${NANO_PACKAGE_DIR} ]; then
		echo "DONE 0 packages"
		return 0
	fi

	# Find a pkg-* package
	for x in `find -s ${NANO_PACKAGE_DIR} -iname 'pkg-*'`; do
		_NANO_PKG_PACKAGE=`basename "$x"`
	done
	if [ -z "${_NANO_PKG_PACKAGE}" -o ! -f "${NANO_PACKAGE_DIR}/${_NANO_PKG_PACKAGE}" ]; then
		echo "FAILED: need a pkg/ package for bootstrapping"
		exit 2
	fi

	# Mount packages into chroot
	mkdir -p ${NANO_WORLDDIR}/_.p
	mount -t nullfs -o noatime -o ro ${NANO_PACKAGE_DIR} ${NANO_WORLDDIR}/_.p

	trap "umount ${NANO_WORLDDIR}/_.p ; rm -rf ${NANO_WORLDDIR}/_.p" 1 2 15 EXIT

	# Install pkg-* package
	CR "${PKGCMD} add /_.p/${_NANO_PKG_PACKAGE}"

	(
		# Expand any glob characters in pacakge list
		cd "${NANO_PACKAGE_DIR}"
		_PKGS=`find ${NANO_PACKAGE_LIST} -not -name "${_NANO_PKG_PACKAGE}" -print | sort | uniq`

		# Show todo
		todo=`echo "$_PKGS" | wc -l`
		echo "=== TODO: $todo"
		echo "$_PKGS"
		echo "==="

		# Install packages
		for _PKG in $_PKGS; do
			CR "${PKGCMD} add /_.p/${_PKG}"
		done
	)

	CR0 "${PKGCMD} info"

	trap - 1 2 15 EXIT
	umount ${NANO_WORLDDIR}/_.p
	rm -rf ${NANO_WORLDDIR}/_.p
)

#######################################################################
# Convenience function:
#	Register all args as early customize function to run just before
#	build commences.

early_customize_cmd ( ) {
	NANO_EARLY_CUSTOMIZE="$NANO_EARLY_CUSTOMIZE $*"
}

#######################################################################
# Convenience function:
# 	Register all args as customize function.

customize_cmd ( ) {
	NANO_CUSTOMIZE="$NANO_CUSTOMIZE $*"
}

#######################################################################
# Convenience function:
# 	Register all args as late customize function to run just before
#	image creation.

late_customize_cmd ( ) {
	NANO_LATE_CUSTOMIZE="$NANO_LATE_CUSTOMIZE $*"
}

#######################################################################
#
# All set up to go...
#
#######################################################################

# Progress Print
#	Print $2 at level $1.
pprint ( ) (
    if [ "$1" -le $PPLEVEL ]; then
	runtime=$(( `date +%s` - $NANO_STARTTIME ))
	printf "%s %.${1}s %s\n" "`date -u -r $runtime +%H:%M:%S`" "#####" "$2" 1>&3
    fi
)

usage ( ) {
	(
	echo "Usage: $0 [-bfhiKknqvwX] [-c config_file]"
	echo "	-b	suppress builds (both kernel and world)"
	echo "	-c	specify config file"
	echo "	-f	suppress code slice extraction"
	echo "	-h	print this help summary page"
	echo "	-i	suppress disk image build"
	echo "	-K	suppress installkernel"
	echo "	-k	suppress buildkernel"
	echo "	-n	add -DNO_CLEAN to buildworld, buildkernel, etc"
	echo "	-q	make output more quiet"
	echo "	-v	make output more verbose"
	echo "	-w	suppress buildworld"
	echo "	-X	make native-xtools"
	) 1>&2
	exit 2
}

#######################################################################
# Setup and Export Internal variables
#

export_var ( ) {		# Don't wawnt a subshell
	var=$1
	# Lookup value of the variable.
	eval val=\$$var
	pprint 3 "Setting variable: $var=\"$val\""
	export $1
}

# Call this function to set defaults _after_ parsing options.
# dont want a subshell otherwise variable setting is thrown away.
set_defaults_and_export ( ) {
	: ${NANO_OBJ:=/usr/obj/nanobsd.${NANO_NAME}}
	: ${MAKEOBJDIRPREFIX:=${NANO_OBJ}}
	: ${NANO_DISKIMGDIR:=${NANO_OBJ}}
	: ${NANO_WORLDDIR:=${NANO_OBJ}/_.w}
	: ${NANO_LOG:=${NANO_OBJ}}
	NANO_MAKE_CONF_BUILD=${MAKEOBJDIRPREFIX}/make.conf.build
	NANO_MAKE_CONF_INSTALL=${NANO_OBJ}/make.conf.install

	# Override user's NANO_DRIVE if they specified a NANO_LABEL
	[ -n "${NANO_LABEL}" ] && NANO_DRIVE="ufs/${NANO_LABEL}" || true

	# Set a default NANO_TOOLS to NANO_SRC/NANO_TOOLS if it exists.
	[ ! -d "${NANO_TOOLS}" ] && [ -d "${NANO_SRC}/${NANO_TOOLS}" ] && \
		NANO_TOOLS="${NANO_SRC}/${NANO_TOOLS}" || true

	[ -n "${NANO_NOPRIV_BUILD}" ] && [ -z "${NANO_METALOG}" ] && \
		NANO_METALOG=${NANO_OBJ}/_.metalog || true

	NANO_STARTTIME=`date +%s`
	pprint 3 "Exporting NanoBSD variables"
	export_var MAKEOBJDIRPREFIX
	export_var NANO_ARCH
	export_var NANO_CODESIZE
	export_var NANO_CONFSIZE
	export_var NANO_CUSTOMIZE
	export_var NANO_DATASIZE
	export_var NANO_DRIVE
	export_var NANO_HEADS
	export_var NANO_IMAGES
	export_var NANO_IMGNAME
	export_var NANO_IMG1NAME
	export_var NANO_MAKE
	export_var NANO_MAKE_CONF_BUILD
	export_var NANO_MAKE_CONF_INSTALL
	export_var NANO_MEDIASIZE
	export_var NANO_NAME
	export_var NANO_NEWFS
	export_var NANO_OBJ
	export_var NANO_PMAKE
	export_var NANO_SECTS
	export_var NANO_SRC
	export_var NANO_TOOLS
	export_var NANO_WORLDDIR
	export_var NANO_BOOT0CFG
	export_var NANO_BOOTLOADER
	export_var NANO_LABEL
	export_var NANO_MODULES
	export_var NANO_NOPRIV_BUILD
	export_var NANO_METALOG
	export_var NANO_LOG
	export_var SRCCONF
	export_var SRC_ENV_CONF
}
