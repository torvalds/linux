#!/bin/sh
#-
# Copyright (c) 2013-2018 The FreeBSD Foundation
# Copyright (c) 2013 Glen Barber
# Copyright (c) 2011 Nathan Whitehorn
# All rights reserved.
#
# Portions of this software were developed by Glen Barber
# under sponsorship from the FreeBSD Foundation.
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
# release.sh: check out source trees, and build release components with
#  totally clean, fresh trees.
# Based on release/generate-release.sh written by Nathan Whitehorn
#
# $FreeBSD$
#

export PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin"

VERSION=2

# Prototypes that can be redefined per-chroot or per-target.
load_chroot_env() { }
load_target_env() { }
buildenv_setup() { }

usage() {
	echo "Usage: $0 [-c release.conf]"
	exit 1
}

# env_setup(): Set up the default build environment variables, such as the
# CHROOTDIR, VCSCMD, SVNROOT, etc.  This is called before the release.conf
# file is sourced, if '-c <release.conf>' is specified.
env_setup() {
	# The directory within which the release will be built.
	CHROOTDIR="/scratch"
	RELENGDIR="$(dirname $(realpath ${0}))"

	# The default version control system command to obtain the sources.
	for _dir in /usr/bin /usr/local/bin; do
		for _svn in svn svnlite; do
			[ -x "${_dir}/${_svn}" ] && VCSCMD="${_dir}/${_svn}"
			[ ! -z "${VCSCMD}" ] && break 2
		done
	done
	VCSCMD="${VCSCMD} checkout"

	# The default svn checkout server, and svn branches for src/, doc/,
	# and ports/.
	SVNROOT="svn://svn.FreeBSD.org/"
	SRCBRANCH="base/head@rHEAD"
	DOCBRANCH="doc/head@rHEAD"
	PORTBRANCH="ports/head@rHEAD"

	# Set for embedded device builds.
	EMBEDDEDBUILD=

	# Sometimes one needs to checkout src with --force svn option.
	# If custom kernel configs copied to src tree before checkout, e.g.
	SRC_FORCE_CHECKOUT=

	# The default make.conf and src.conf to use.  Set to /dev/null
	# by default to avoid polluting the chroot(8) environment with
	# non-default settings.
	MAKE_CONF="/dev/null"
	SRC_CONF="/dev/null"

	# The number of make(1) jobs, defaults to the number of CPUs available
	# for buildworld, and half of number of CPUs available for buildkernel.
	WORLD_FLAGS="-j$(sysctl -n hw.ncpu)"
	KERNEL_FLAGS="-j$(( $(( $(sysctl -n hw.ncpu) + 1 )) / 2))"

	MAKE_FLAGS="-s"

	# The name of the kernel to build, defaults to GENERIC.
	KERNEL="GENERIC"

	# Set to non-empty value to disable checkout of doc/ and/or ports/.
	# Disabling ports/ checkout also forces NODOC to be set.
	NODOC=
	NOPORTS=

	# Set to non-empty value to disable distributing source tree.
	NOSRC=

	# Set to non-empty value to build dvd1.iso as part of the release.
	WITH_DVD=
	WITH_COMPRESSED_IMAGES=

	# Set to non-empty value to build virtual machine images as part of
	# the release.
	WITH_VMIMAGES=
	WITH_COMPRESSED_VMIMAGES=
	XZ_THREADS=0

	# Set to non-empty value to build virtual machine images for various
	# cloud providers as part of the release.
	WITH_CLOUDWARE=

	return 0
} # env_setup()

# env_check(): Perform sanity tests on the build environment, such as ensuring
# files/directories exist, as well as adding backwards-compatibility hacks if
# necessary.  This is called unconditionally, and overrides the defaults set
# in env_setup() if '-c <release.conf>' is specified.
env_check() {
	chroot_build_release_cmd="chroot_build_release"
	# Fix for backwards-compatibility with release.conf that does not have
	# the trailing '/'.
	case ${SVNROOT} in
		*svn*)
			SVNROOT="${SVNROOT}/"
			;;
		*)
			;;
	esac

	# Prefix the branches with the SVNROOT for the full checkout URL.
	SRCBRANCH="${SVNROOT}${SRCBRANCH}"
	DOCBRANCH="${SVNROOT}${DOCBRANCH}"
	PORTBRANCH="${SVNROOT}${PORTBRANCH}"

	if [ -n "${EMBEDDEDBUILD}" ]; then
		WITH_DVD=
		WITH_COMPRESSED_IMAGES=
		NODOC=yes
		case ${EMBEDDED_TARGET}:${EMBEDDED_TARGET_ARCH} in
			arm:arm*|arm64:aarch64)
				chroot_build_release_cmd="chroot_arm_build_release"
				;;
			*)
				;;
		esac
	fi

	# If PORTS is set and NODOC is unset, force NODOC=yes because the ports
	# tree is required to build the documentation set.
	if [ -n "${NOPORTS}" ] && [ -z "${NODOC}" ]; then
		echo "*** NOTICE: Setting NODOC=1 since ports tree is required"
		echo "            and NOPORTS is set."
		NODOC=yes
	fi

	# If NOSRC, NOPORTS and/or NODOC are unset, they must not pass to make
	# as variables.  The release makefile verifies definedness of the
	# NOPORTS/NODOC variables instead of their values.
	SRCDOCPORTS=
	if [ -n "${NOPORTS}" ]; then
		SRCDOCPORTS="NOPORTS=yes"
	fi
	if [ -n "${NODOC}" ]; then
		SRCDOCPORTS="${SRCDOCPORTS}${SRCDOCPORTS:+ }NODOC=yes"
	fi
	if [ -n "${NOSRC}" ]; then
		SRCDOCPORTS="${SRCDOCPORTS}${SRCDOCPORTS:+ }NOSRC=yes"
	fi

	# The aggregated build-time flags based upon variables defined within
	# this file, unless overridden by release.conf.  In most cases, these
	# will not need to be changed.
	CONF_FILES="__MAKE_CONF=${MAKE_CONF} SRCCONF=${SRC_CONF}"
	if [ -n "${TARGET}" ] && [ -n "${TARGET_ARCH}" ]; then
		ARCH_FLAGS="TARGET=${TARGET} TARGET_ARCH=${TARGET_ARCH}"
	else
		ARCH_FLAGS=
	fi
	# Force src checkout if configured
	FORCE_SRC_KEY=
	if [ -n "${SRC_FORCE_CHECKOUT}" ]; then
		FORCE_SRC_KEY="--force"
	fi

	if [ -z "${CHROOTDIR}" ]; then
		echo "Please set CHROOTDIR."
		exit 1
	fi

	if [ $(id -u) -ne 0 ]; then
		echo "Needs to be run as root."
		exit 1
	fi

	# Unset CHROOTBUILD_SKIP if the chroot(8) does not appear to exist.
	if [ ! -z "${CHROOTBUILD_SKIP}" -a ! -e ${CHROOTDIR}/bin/sh ]; then
		CHROOTBUILD_SKIP=
	fi

	CHROOT_MAKEENV="${CHROOT_MAKEENV} \
		MAKEOBJDIRPREFIX=${CHROOTDIR}/tmp/obj"
	CHROOT_WMAKEFLAGS="${MAKE_FLAGS} ${WORLD_FLAGS} ${CONF_FILES}"
	CHROOT_IMAKEFLAGS="${WORLD_FLAGS} ${CONF_FILES}"
	CHROOT_DMAKEFLAGS="${WORLD_FLAGS} ${CONF_FILES}"
	RELEASE_WMAKEFLAGS="${MAKE_FLAGS} ${WORLD_FLAGS} ${ARCH_FLAGS} \
		${CONF_FILES}"
	RELEASE_KMAKEFLAGS="${MAKE_FLAGS} ${KERNEL_FLAGS} \
		KERNCONF=\"${KERNEL}\" ${ARCH_FLAGS} ${CONF_FILES}"
	RELEASE_RMAKEFLAGS="${ARCH_FLAGS} \
		KERNCONF=\"${KERNEL}\" ${CONF_FILES} ${SRCDOCPORTS} \
		WITH_DVD=${WITH_DVD} WITH_VMIMAGES=${WITH_VMIMAGES} \
		WITH_CLOUDWARE=${WITH_CLOUDWARE} XZ_THREADS=${XZ_THREADS}"

	return 0
} # env_check()

# chroot_setup(): Prepare the build chroot environment for the release build.
chroot_setup() {
	load_chroot_env
	mkdir -p ${CHROOTDIR}/usr

	if [ -z "${SRC_UPDATE_SKIP}" ]; then
		${VCSCMD} ${FORCE_SRC_KEY} ${SRCBRANCH} ${CHROOTDIR}/usr/src
	fi
	if [ -z "${NODOC}" ] && [ -z "${DOC_UPDATE_SKIP}" ]; then
		${VCSCMD} ${DOCBRANCH} ${CHROOTDIR}/usr/doc
	fi
	if [ -z "${NOPORTS}" ] && [ -z "${PORTS_UPDATE_SKIP}" ]; then
		${VCSCMD} ${PORTBRANCH} ${CHROOTDIR}/usr/ports
	fi

	if [ -z "${CHROOTBUILD_SKIP}" ]; then
		cd ${CHROOTDIR}/usr/src
		env ${CHROOT_MAKEENV} make ${CHROOT_WMAKEFLAGS} buildworld
		env ${CHROOT_MAKEENV} make ${CHROOT_IMAKEFLAGS} installworld \
			DESTDIR=${CHROOTDIR}
		env ${CHROOT_MAKEENV} make ${CHROOT_DMAKEFLAGS} distribution \
			DESTDIR=${CHROOTDIR}
	fi

	return 0
} # chroot_setup()

# extra_chroot_setup(): Prepare anything additional within the build
# necessary for the release build.
extra_chroot_setup() {
	mkdir -p ${CHROOTDIR}/dev
	mount -t devfs devfs ${CHROOTDIR}/dev
	[ -e /etc/resolv.conf -a ! -e ${CHROOTDIR}/etc/resolv.conf ] && \
		cp /etc/resolv.conf ${CHROOTDIR}/etc/resolv.conf
	# Run ldconfig(8) in the chroot directory so /var/run/ld-elf*.so.hints
	# is created.  This is needed by ports-mgmt/pkg.
	eval chroot ${CHROOTDIR} /etc/rc.d/ldconfig forcerestart

	# If MAKE_CONF and/or SRC_CONF are set and not character devices
	# (/dev/null), copy them to the chroot.
	if [ -e ${MAKE_CONF} ] && [ ! -c ${MAKE_CONF} ]; then
		mkdir -p ${CHROOTDIR}/$(dirname ${MAKE_CONF})
		cp ${MAKE_CONF} ${CHROOTDIR}/${MAKE_CONF}
	fi
	if [ -e ${SRC_CONF} ] && [ ! -c ${SRC_CONF} ]; then
		mkdir -p ${CHROOTDIR}/$(dirname ${SRC_CONF})
		cp ${SRC_CONF} ${CHROOTDIR}/${SRC_CONF}
	fi

	if [ -d ${CHROOTDIR}/usr/ports ]; then
		# Trick the ports 'run-autotools-fixup' target to do the right
		# thing.
		_OSVERSION=$(chroot ${CHROOTDIR} /usr/bin/uname -U)
		REVISION=$(chroot ${CHROOTDIR} make -C /usr/src/release -V REVISION)
		BRANCH=$(chroot ${CHROOTDIR} make -C /usr/src/release -V BRANCH)
		UNAME_r=${REVISION}-${BRANCH}
		if [ -d ${CHROOTDIR}/usr/doc ] && [ -z "${NODOC}" ]; then
			PBUILD_FLAGS="OSVERSION=${_OSVERSION} BATCH=yes"
			PBUILD_FLAGS="${PBUILD_FLAGS} UNAME_r=${UNAME_r}"
			PBUILD_FLAGS="${PBUILD_FLAGS} OSREL=${REVISION}"
			PBUILD_FLAGS="${PBUILD_FLAGS} WRKDIRPREFIX=/tmp/ports"
			PBUILD_FLAGS="${PBUILD_FLAGS} DISTDIR=/tmp/distfiles"
			chroot ${CHROOTDIR} env ${PBUILD_FLAGS} \
				OPTIONS_UNSET="AVAHI FOP IGOR" make -C \
				/usr/ports/textproc/docproj \
				FORCE_PKG_REGISTER=1 \
				install clean distclean
		fi
	fi

	if [ ! -z "${EMBEDDEDPORTS}" ]; then
		_OSVERSION=$(chroot ${CHROOTDIR} /usr/bin/uname -U)
		REVISION=$(chroot ${CHROOTDIR} make -C /usr/src/release -V REVISION)
		BRANCH=$(chroot ${CHROOTDIR} make -C /usr/src/release -V BRANCH)
		PBUILD_FLAGS="OSVERSION=${_OSVERSION} BATCH=yes"
		PBUILD_FLAGS="${PBUILD_FLAGS} UNAME_r=${UNAME_r}"
		PBUILD_FLAGS="${PBUILD_FLAGS} OSREL=${REVISION}"
		PBUILD_FLAGS="${PBUILD_FLAGS} WRKDIRPREFIX=/tmp/ports"
		PBUILD_FLAGS="${PBUILD_FLAGS} DISTDIR=/tmp/distfiles"
		for _PORT in ${EMBEDDEDPORTS}; do
			eval chroot ${CHROOTDIR} env ${PBUILD_FLAGS} make -C \
				/usr/ports/${_PORT} \
				FORCE_PKG_REGISTER=1 deinstall install clean distclean
		done
	fi

	buildenv_setup

	return 0
} # extra_chroot_setup()

# chroot_build_target(): Build the userland and kernel for the build target.
chroot_build_target() {
	load_target_env
	if [ ! -z "${EMBEDDEDBUILD}" ]; then
		RELEASE_WMAKEFLAGS="${RELEASE_WMAKEFLAGS} \
			TARGET=${EMBEDDED_TARGET} \
			TARGET_ARCH=${EMBEDDED_TARGET_ARCH}"
		RELEASE_KMAKEFLAGS="${RELEASE_KMAKEFLAGS} \
			TARGET=${EMBEDDED_TARGET} \
			TARGET_ARCH=${EMBEDDED_TARGET_ARCH}"
	fi
	eval chroot ${CHROOTDIR} make -C /usr/src ${RELEASE_WMAKEFLAGS} buildworld
	eval chroot ${CHROOTDIR} make -C /usr/src ${RELEASE_KMAKEFLAGS} buildkernel

	return 0
} # chroot_build_target

# chroot_build_release(): Invoke the 'make release' target.
chroot_build_release() {
	load_target_env
	if [ ! -z "${WITH_VMIMAGES}" ]; then
		if [ -z "${VMFORMATS}" ]; then
			VMFORMATS="$(eval chroot ${CHROOTDIR} \
				make -C /usr/src/release -V VMFORMATS)"
		fi
		if [ -z "${VMSIZE}" ]; then
			VMSIZE="$(eval chroot ${CHROOTDIR} \
				make -C /usr/src/release -V VMSIZE)"
		fi
		RELEASE_RMAKEFLAGS="${RELEASE_RMAKEFLAGS} \
			VMFORMATS=\"${VMFORMATS}\" VMSIZE=${VMSIZE}"
	fi
	eval chroot ${CHROOTDIR} make -C /usr/src/release \
		${RELEASE_RMAKEFLAGS} release
	eval chroot ${CHROOTDIR} make -C /usr/src/release \
		${RELEASE_RMAKEFLAGS} install DESTDIR=/R \
		WITH_COMPRESSED_IMAGES=${WITH_COMPRESSED_IMAGES} \
		WITH_COMPRESSED_VMIMAGES=${WITH_COMPRESSED_VMIMAGES}

	return 0
} # chroot_build_release()

efi_boot_name()
{
	case $1 in
		arm)
			echo "bootarm.efi"
			;;
		arm64)
			echo "bootaa64.efi"
			;;
		amd64)
			echo "bootx86.efi"
			;;
	esac
}

# chroot_arm_build_release(): Create arm SD card image.
chroot_arm_build_release() {
	load_target_env
	case ${EMBEDDED_TARGET} in
		arm|arm64)
			if [ -e "${RELENGDIR}/tools/arm.subr" ]; then
				. "${RELENGDIR}/tools/arm.subr"
			fi
			;;
		*)
			;;
	esac
	[ ! -z "${RELEASECONF}" ] && . "${RELEASECONF}"
	export MAKE_FLAGS="${MAKE_FLAGS} TARGET=${EMBEDDED_TARGET}"
	export MAKE_FLAGS="${MAKE_FLAGS} TARGET_ARCH=${EMBEDDED_TARGET_ARCH}"
	eval chroot ${CHROOTDIR} env WITH_UNIFIED_OBJDIR=1 make ${MAKE_FLAGS} -C /usr/src/release obj
	export WORLDDIR="$(eval chroot ${CHROOTDIR} make ${MAKE_FLAGS} -C /usr/src/release -V WORLDDIR)"
	export OBJDIR="$(eval chroot ${CHROOTDIR} env WITH_UNIFIED_OBJDIR=1 make ${MAKE_FLAGS} -C /usr/src/release -V .OBJDIR)"
	export DESTDIR="${OBJDIR}/${KERNEL}"
	export IMGBASE="${CHROOTDIR}/${OBJDIR}/${BOARDNAME}.img"
	export OSRELEASE="$(eval chroot ${CHROOTDIR} make ${MAKE_FLAGS} -C /usr/src/release \
		TARGET=${EMBEDDED_TARGET} TARGET_ARCH=${EMBEDDED_TARGET_ARCH} \
		-V OSRELEASE)"
	chroot ${CHROOTDIR} mkdir -p ${DESTDIR}
	chroot ${CHROOTDIR} truncate -s ${IMAGE_SIZE} ${IMGBASE##${CHROOTDIR}}
	export mddev=$(chroot ${CHROOTDIR} \
		mdconfig -f ${IMGBASE##${CHROOTDIR}} ${MD_ARGS})
	arm_create_disk
	arm_install_base
	arm_install_boot
	arm_install_uboot
	mdconfig -d -u ${mddev}
	chroot ${CHROOTDIR} rmdir ${DESTDIR}
	mv ${IMGBASE} ${CHROOTDIR}/${OBJDIR}/${OSRELEASE}-${BOARDNAME}.img
	chroot ${CHROOTDIR} mkdir -p /R
	chroot ${CHROOTDIR} cp -p ${OBJDIR}/${OSRELEASE}-${BOARDNAME}.img \
		/R/${OSRELEASE}-${BOARDNAME}.img
	chroot ${CHROOTDIR} xz -T ${XZ_THREADS} /R/${OSRELEASE}-${BOARDNAME}.img
	cd ${CHROOTDIR}/R && sha512 ${OSRELEASE}* \
		> CHECKSUM.SHA512
	cd ${CHROOTDIR}/R && sha256 ${OSRELEASE}* \
		> CHECKSUM.SHA256

	return 0
} # chroot_arm_build_release()

# main(): Start here.
main() {
	set -e # Everything must succeed
	env_setup
	while getopts c: opt; do
		case ${opt} in
			c)
				RELEASECONF="$(realpath ${OPTARG})"
				;;
			\?)
				usage
				;;
		esac
	done
	shift $(($OPTIND - 1))
	if [ ! -z "${RELEASECONF}" ]; then
		if [ -e "${RELEASECONF}" ]; then
			. ${RELEASECONF}
		else
			echo "Nonexistent configuration file: ${RELEASECONF}"
			echo "Using default build environment."
		fi
	fi
	env_check
	trap "umount ${CHROOTDIR}/dev" EXIT # Clean up devfs mount on exit
	chroot_setup
	extra_chroot_setup
	chroot_build_target
	${chroot_build_release_cmd}

	return 0
} # main()

main "${@}"
