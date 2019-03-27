#!/bin/sh
#
# Copyright (c) 2016 Will Andrews
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer 
#    in this position and unchanged.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$
#
##
# Install a boot environment using the current FreeBSD source tree.
# Requires a fully built world & kernel.
#
# Non-base tools required: beadm, pkg
#
# In a sandbox for the new boot environment, this script also runs etcupdate
# and pkg upgrade automatically in the sandbox.  Upon successful completion,
# the system will be ready to boot into the new boot environment.  Upon
# failure, the target boot environment will be destroyed.  In all cases, the
# running system is left untouched.
#
## Usage:
# beinstall [optional world/kernel flags e.g. KERNCONF]
#
## User modifiable variables - set these in the environment if desired.
# If not empty, 'pkg upgrade' will be skipped.
NO_PKG_UPGRADE="${NO_PKG_UPGRADE:-""}"
# Config updater - 'etcupdate' and 'mergemaster' are supported.  Set to an
# empty string to skip.
CONFIG_UPDATER="${CONFIG_UPDATER:-"etcupdate"}"
# Flags for etcupdate if used.
ETCUPDATE_FLAGS="${ETCUPDATE_FLAGS:-"-F"}"
# Flags for mergemaster if used.
MERGEMASTER_FLAGS="${MERGEMASTER_FLAGS:-"-iFU"}"


########################################################################
## Constants
ETCUPDATE_CMD="etcupdate"
MERGEMASTER_CMD="mergemaster"

## Functions
cleanup() {
	[ -z "${cleanup_commands}" ] && return
	echo "Cleaning up ..."
	for command in ${cleanup_commands}; do
		${command}
	done
}

errx() {
	cleanup
	echo "error: $@"
	exit 1
}

rmdir_be() {
	chflags -R noschg ${BE_MNTPT}
	rm -rf ${BE_MNTPT}
}

unmount_be() {
	mount | grep " on ${BE_MNTPT}" | awk '{print $3}' | sort -r | xargs -t umount -f
}

copy_pkgs() {
	# Before cleaning up, try to save progress in pkg(8) updates, to
	# speed up future updates.  This is only called on the error path;
	# no need to run on success.
	echo "Rsyncing back newly saved packages..."
	rsync -av --progress ${BE_MNTPT}/var/cache/pkg/. /var/cache/pkg/.
}

cleanup_be() {
	# Before destroying, unmount any child filesystems that may have
	# been mounted under the boot environment.  Sort them in reverse
	# order so children are unmounted first.
	unmount_be
	# Finally, clean up any directories that were created by the
	# operation, via cleanup_be_dirs().
	if [ -n "${created_be_dirs}" ]; then
		chroot ${BE_MNTPT} /bin/rm -rf ${created_be_dirs}
	fi
	beadm destroy -F ${BENAME}
}

create_be_dirs() {
	echo "${BE_MNTPT}: Inspecting dirs $*"
	for dir in $*; do
		curdir="$dir"
		topdir="$dir"
		while :; do
			[ -e "${BE_MNTPT}${curdir}" ] && break
			topdir=$curdir
			curdir=$(dirname ${curdir})
		done
		[ "$curdir" = "$dir" ] && continue

		# Add the top-most nonexistent directory to the list, then
		# mkdir -p the innermost directory specified by the argument.
		# This way the least number of directories are rm'd directly.
		created_be_dirs="${topdir} ${created_be_dirs}"
		echo "${BE_MNTPT}: Created ${dir}"
		mkdir -p ${BE_MNTPT}${dir} || return $?
	done
	return 0
}

update_mergemaster_pre() {
	mergemaster -p -m ${srcdir} -D ${BE_MNTPT} -t ${BE_MM_ROOT} ${MERGEMASTER_FLAGS}
}

update_mergemaster() {
	chroot ${BE_MNTPT} \
		mergemaster -m ${srcdir} -t ${BE_MM_ROOT} ${MERGEMASTER_FLAGS}
}

update_etcupdate_pre() {
	etcupdate -p -s ${srcdir} -D ${BE_MNTPT} ${ETCUPDATE_FLAGS} || return $?
	etcupdate resolve -D ${BE_MNTPT} || return $?
}

update_etcupdate() {
	chroot ${BE_MNTPT} \
		etcupdate -s ${srcdir} ${ETCUPDATE_FLAGS} || return $?
	chroot ${BE_MNTPT} etcupdate resolve
}


# Special command-line subcommand that can be used to do a full cleanup
# after a manual post-mortem has been completed.
postmortem() {
	[ -n "${BENAME}" ] || errx "Must specify BENAME"
	[ -n "${BE_MNTPT}" ] || errx "Must specify BE_MNTPT"
	echo "Performing post-mortem on BE ${BENAME} at ${BE_MNTPT} ..."
	unmount_be
	rmdir_be
	echo "Post-mortem cleanup complete."
	echo "To destroy the BE (recommended), run: beadm destroy ${BENAME}"
	echo "To instead continue with the BE, run: beadm activate ${BENAME}"
}

if [ -n "$BEINSTALL_CMD" ]; then
	${BEINSTALL_CMD} $*
	exit $?
fi


cleanup_commands=""
trap 'errx "Interrupt caught"' HUP INT TERM

[ "$(whoami)" != "root" ] && errx "Must be run as root"

[ ! -f "Makefile.inc1" ] && errx "Must be in FreeBSD source tree"
srcdir=$(pwd)
objdir=$(make -V .OBJDIR 2>/dev/null)
[ ! -d "${objdir}" ] && errx "Must have built FreeBSD from source tree"

# May be a worktree, in which case .git is a file, not a directory.
if [ -e .git ] ; then
    commit_time=$(git show --format='%ct' 2>/dev/null | head -1)
    [ $? -ne 0 ] && errx "Can't lookup git commit timestamp"
    commit_ts=$(date -r ${commit_time} '+%Y%m%d.%H%M%S')
elif [ -d .svn ] ; then
      if [ -e /usr/bin/svnlite ]; then
        svn=/usr/bin/svnlite
      elif [ -e /usr/local/bin/svn ]; then
        svn=/usr/local/bin/svn
      else
        errx "Unable to find subversion"
      fi
      commit_ts="$( "$svn" info --show-item last-changed-date | sed -e 's/\..*//' -e 's/T/./' -e 's/-//g' -e s'/://g' )"
    [ $? -ne 0 ] && errx "Can't lookup Subversion commit timestamp"
else
    errx "Unable to determine source control type"
fi

commit_ver=$(${objdir}/bin/freebsd-version/freebsd-version -u 2>/dev/null)
[ -z "${commit_ver}" ] && errx "Unable to determine FreeBSD version"

BENAME="${commit_ver}-${commit_ts}"

BE_TMP=$(mktemp -d /tmp/beinstall.XXXXXX)
[ $? -ne 0 -o ! -d ${BE_TMP} ] && errx "Unable to create mountpoint"
[ -z "$NO_CLEANUP_BE" ] && cleanup_commands="rmdir_be ${cleanup_commands}"
BE_MNTPT=${BE_TMP}/mnt
BE_MM_ROOT=${BE_TMP}/mergemaster # mergemaster will create
mkdir -p ${BE_MNTPT}

beadm create ${BENAME} >/dev/null || errx "Unable to create BE ${BENAME}"
[ -z "$NO_CLEANUP_BE" ] && cleanup_commands="cleanup_be ${cleanup_commands}"

beadm mount ${BENAME} ${BE_TMP}/mnt || errx "Unable to mount BE ${BENAME}."

echo "Mounted ${BENAME} to ${BE_MNTPT}, performing install/update ..."
make "$@" DESTDIR=${BE_MNTPT} installkernel || errx "Installkernel failed!"
if [ -n "${CONFIG_UPDATER}" ]; then
	"update_${CONFIG_UPDATER}_pre"
	[ $? -ne 0 ] && errx "${CONFIG_UPDATER} (pre-world) failed!"
fi

# Mount the source and object tree within the BE in order to account for any
# changes applied by the pre-installworld updater.  Cleanup any directories
# created if they didn't exist previously.
create_be_dirs "${srcdir}" "${objdir}" || errx "Unable to create BE dirs"
mount -t nullfs "${srcdir}" "${BE_MNTPT}${srcdir}" || errx "Unable to mount src"
mount -t nullfs "${objdir}" "${BE_MNTPT}${objdir}" || errx "Unable to mount obj"

chroot ${BE_MNTPT} make "$@" -C ${srcdir} installworld || \
	errx "Installworld failed!"

if [ -n "${CONFIG_UPDATER}" ]; then
	"update_${CONFIG_UPDATER}"
	[ $? -ne 0 ] && errx "${CONFIG_UPDATER} (post-world) failed!"
fi

if which rsync >/dev/null 2>&1; then
	cleanup_commands="copy_pkgs ${cleanup_commands}"
fi

BE_PKG="chroot ${BE_MNTPT} env ASSUME_ALWAYS_YES=true pkg"
if [ -z "${NO_PKG_UPGRADE}" ]; then
	${BE_PKG} update || errx "Unable to update pkg"
	${BE_PKG} upgrade || errx "Unable to upgrade pkgs"
fi

if [ -n "$NO_CLEANUP_BE" ]; then
	echo "Boot Environment ${BENAME} may be examined in ${BE_MNTPT}."
	echo "Afterwards, run this to cleanup:"
	echo "  env BENAME=${BENAME} BE_MNTPT=${BE_MNTPT} BEINSTALL_CMD=postmortem $0"
	exit 0
fi

unmount_be || errx "Unable to unmount BE"
rmdir_be || errx "Unable to cleanup BE"
beadm activate ${BENAME} || errx "Unable to activate BE"
echo
beadm list
echo
echo "Boot environment ${BENAME} setup complete; reboot to use it."
