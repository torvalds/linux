#!/bin/sh

#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright 2004-2007 Colin Percival
# All rights reserved
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted providing that the following conditions 
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# $FreeBSD$

#### Usage function -- called from command-line handling code.

# Usage instructions.  Options not listed:
# --debug	-- don't filter output from utilities
# --no-stats	-- don't show progress statistics while fetching files
usage () {
	cat <<EOF
usage: `basename $0` [options] command ... [path]

Options:
  -b basedir   -- Operate on a system mounted at basedir
                  (default: /)
  -d workdir   -- Store working files in workdir
                  (default: /var/db/freebsd-update/)
  -f conffile  -- Read configuration options from conffile
                  (default: /etc/freebsd-update.conf)
  -F           -- Force a fetch operation to proceed in the
                  case of an unfinished upgrade
  -k KEY       -- Trust an RSA key with SHA256 hash of KEY
  -r release   -- Target for upgrade (e.g., 11.1-RELEASE)
  -s server    -- Server from which to fetch updates
                  (default: update.FreeBSD.org)
  -t address   -- Mail output of cron command, if any, to address
                  (default: root)
  --not-running-from-cron
               -- Run without a tty, for use by automated tools
  --currently-running release
               -- Update as if currently running this release
Commands:
  fetch        -- Fetch updates from server
  cron         -- Sleep rand(3600) seconds, fetch updates, and send an
                  email if updates were found
  upgrade      -- Fetch upgrades to FreeBSD version specified via -r option
  install      -- Install downloaded updates or upgrades
  rollback     -- Uninstall most recently installed updates
  IDS          -- Compare the system against an index of "known good" files.
EOF
	exit 0
}

#### Configuration processing functions

#-
# Configuration options are set in the following order of priority:
# 1. Command line options
# 2. Configuration file options
# 3. Default options
# In addition, certain options (e.g., IgnorePaths) can be specified multiple
# times and (as long as these are all in the same place, e.g., inside the
# configuration file) they will accumulate.  Finally, because the path to the
# configuration file can be specified at the command line, the entire command
# line must be processed before we start reading the configuration file.
#
# Sound like a mess?  It is.  Here's how we handle this:
# 1. Initialize CONFFILE and all the options to "".
# 2. Process the command line.  Throw an error if a non-accumulating option
#    is specified twice.
# 3. If CONFFILE is "", set CONFFILE to /etc/freebsd-update.conf .
# 4. For all the configuration options X, set X_saved to X.
# 5. Initialize all the options to "".
# 6. Read CONFFILE line by line, parsing options.
# 7. For each configuration option X, set X to X_saved iff X_saved is not "".
# 8. Repeat steps 4-7, except setting options to their default values at (6).

CONFIGOPTIONS="KEYPRINT WORKDIR SERVERNAME MAILTO ALLOWADD ALLOWDELETE
    KEEPMODIFIEDMETADATA COMPONENTS IGNOREPATHS UPDATEIFUNMODIFIED
    BASEDIR VERBOSELEVEL TARGETRELEASE STRICTCOMPONENTS MERGECHANGES
    IDSIGNOREPATHS BACKUPKERNEL BACKUPKERNELDIR BACKUPKERNELSYMBOLFILES"

# Set all the configuration options to "".
nullconfig () {
	for X in ${CONFIGOPTIONS}; do
		eval ${X}=""
	done
}

# For each configuration option X, set X_saved to X.
saveconfig () {
	for X in ${CONFIGOPTIONS}; do
		eval ${X}_saved=\$${X}
	done
}

# For each configuration option X, set X to X_saved if X_saved is not "".
mergeconfig () {
	for X in ${CONFIGOPTIONS}; do
		eval _=\$${X}_saved
		if ! [ -z "${_}" ]; then
			eval ${X}=\$${X}_saved
		fi
	done
}

# Set the trusted keyprint.
config_KeyPrint () {
	if [ -z ${KEYPRINT} ]; then
		KEYPRINT=$1
	else
		return 1
	fi
}

# Set the working directory.
config_WorkDir () {
	if [ -z ${WORKDIR} ]; then
		WORKDIR=$1
	else
		return 1
	fi
}

# Set the name of the server (pool) from which to fetch updates
config_ServerName () {
	if [ -z ${SERVERNAME} ]; then
		SERVERNAME=$1
	else
		return 1
	fi
}

# Set the address to which 'cron' output will be mailed.
config_MailTo () {
	if [ -z ${MAILTO} ]; then
		MAILTO=$1
	else
		return 1
	fi
}

# Set whether FreeBSD Update is allowed to add files (or directories, or
# symlinks) which did not previously exist.
config_AllowAdd () {
	if [ -z ${ALLOWADD} ]; then
		case $1 in
		[Yy][Ee][Ss])
			ALLOWADD=yes
			;;
		[Nn][Oo])
			ALLOWADD=no
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

# Set whether FreeBSD Update is allowed to remove files/directories/symlinks.
config_AllowDelete () {
	if [ -z ${ALLOWDELETE} ]; then
		case $1 in
		[Yy][Ee][Ss])
			ALLOWDELETE=yes
			;;
		[Nn][Oo])
			ALLOWDELETE=no
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

# Set whether FreeBSD Update should keep existing inode ownership,
# permissions, and flags, in the event that they have been modified locally
# after the release.
config_KeepModifiedMetadata () {
	if [ -z ${KEEPMODIFIEDMETADATA} ]; then
		case $1 in
		[Yy][Ee][Ss])
			KEEPMODIFIEDMETADATA=yes
			;;
		[Nn][Oo])
			KEEPMODIFIEDMETADATA=no
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

# Add to the list of components which should be kept updated.
config_Components () {
	for C in $@; do
		if [ "$C" = "src" ]; then
			if [ -e "${BASEDIR}/usr/src/COPYRIGHT" ]; then
				COMPONENTS="${COMPONENTS} ${C}"
			else
				echo "src component not installed, skipped"
			fi
		else
			COMPONENTS="${COMPONENTS} ${C}"
		fi
	done
}

# Add to the list of paths under which updates will be ignored.
config_IgnorePaths () {
	for C in $@; do
		IGNOREPATHS="${IGNOREPATHS} ${C}"
	done
}

# Add to the list of paths which IDS should ignore.
config_IDSIgnorePaths () {
	for C in $@; do
		IDSIGNOREPATHS="${IDSIGNOREPATHS} ${C}"
	done
}

# Add to the list of paths within which updates will be performed only if the
# file on disk has not been modified locally.
config_UpdateIfUnmodified () {
	for C in $@; do
		UPDATEIFUNMODIFIED="${UPDATEIFUNMODIFIED} ${C}"
	done
}

# Add to the list of paths within which updates to text files will be merged
# instead of overwritten.
config_MergeChanges () {
	for C in $@; do
		MERGECHANGES="${MERGECHANGES} ${C}"
	done
}

# Work on a FreeBSD installation mounted under $1
config_BaseDir () {
	if [ -z ${BASEDIR} ]; then
		BASEDIR=$1
	else
		return 1
	fi
}

# When fetching upgrades, should we assume the user wants exactly the
# components listed in COMPONENTS, rather than trying to guess based on
# what's currently installed?
config_StrictComponents () {
	if [ -z ${STRICTCOMPONENTS} ]; then
		case $1 in
		[Yy][Ee][Ss])
			STRICTCOMPONENTS=yes
			;;
		[Nn][Oo])
			STRICTCOMPONENTS=no
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

# Upgrade to FreeBSD $1
config_TargetRelease () {
	if [ -z ${TARGETRELEASE} ]; then
		TARGETRELEASE=$1
	else
		return 1
	fi
	if echo ${TARGETRELEASE} | grep -qE '^[0-9.]+$'; then
		TARGETRELEASE="${TARGETRELEASE}-RELEASE"
	fi
}

# Pretend current release is FreeBSD $1
config_SourceRelease () {
	UNAME_r=$1
	if echo ${UNAME_r} | grep -qE '^[0-9.]+$'; then
		UNAME_r="${UNAME_r}-RELEASE"
	fi
	export UNAME_r
}

# Define what happens to output of utilities
config_VerboseLevel () {
	if [ -z ${VERBOSELEVEL} ]; then
		case $1 in
		[Dd][Ee][Bb][Uu][Gg])
			VERBOSELEVEL=debug
			;;
		[Nn][Oo][Ss][Tt][Aa][Tt][Ss])
			VERBOSELEVEL=nostats
			;;
		[Ss][Tt][Aa][Tt][Ss])
			VERBOSELEVEL=stats
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

config_BackupKernel () {
	if [ -z ${BACKUPKERNEL} ]; then
		case $1 in
		[Yy][Ee][Ss])
			BACKUPKERNEL=yes
			;;
		[Nn][Oo])
			BACKUPKERNEL=no
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

config_BackupKernelDir () {
	if [ -z ${BACKUPKERNELDIR} ]; then
		if [ -z "$1" ]; then
			echo "BackupKernelDir set to empty dir"
			return 1
		fi

		# We check for some paths which would be extremely odd
		# to use, but which could cause a lot of problems if
		# used.
		case $1 in
		/|/bin|/boot|/etc|/lib|/libexec|/sbin|/usr|/var)
			echo "BackupKernelDir set to invalid path $1"
			return 1
			;;
		/*)
			BACKUPKERNELDIR=$1
			;;
		*)
			echo "BackupKernelDir ($1) is not an absolute path"
			return 1
			;;
		esac
	else
		return 1
	fi
}

config_BackupKernelSymbolFiles () {
	if [ -z ${BACKUPKERNELSYMBOLFILES} ]; then
		case $1 in
		[Yy][Ee][Ss])
			BACKUPKERNELSYMBOLFILES=yes
			;;
		[Nn][Oo])
			BACKUPKERNELSYMBOLFILES=no
			;;
		*)
			return 1
			;;
		esac
	else
		return 1
	fi
}

# Handle one line of configuration
configline () {
	if [ $# -eq 0 ]; then
		return
	fi

	OPT=$1
	shift
	config_${OPT} $@
}

#### Parameter handling functions.

# Initialize parameters to null, just in case they're
# set in the environment.
init_params () {
	# Configration settings
	nullconfig

	# No configuration file set yet
	CONFFILE=""

	# No commands specified yet
	COMMANDS=""

	# Force fetch to proceed
	FORCEFETCH=0

	# Run without a TTY
	NOTTYOK=0

	# Fetched first in a chain of commands
	ISFETCHED=0
}

# Parse the command line
parse_cmdline () {
	while [ $# -gt 0 ]; do
		case "$1" in
		# Location of configuration file
		-f)
			if [ $# -eq 1 ]; then usage; fi
			if [ ! -z "${CONFFILE}" ]; then usage; fi
			shift; CONFFILE="$1"
			;;
		-F)
			FORCEFETCH=1
			;;
		--not-running-from-cron)
			NOTTYOK=1
			;;
		--currently-running)
			shift
			config_SourceRelease $1 || usage
			;;

		# Configuration file equivalents
		-b)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_BaseDir $1 || usage
			;;
		-d)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_WorkDir $1 || usage
			;;
		-k)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_KeyPrint $1 || usage
			;;
		-s)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_ServerName $1 || usage
			;;
		-r)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_TargetRelease $1 || usage
			;;
		-t)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_MailTo $1 || usage
			;;
		-v)
			if [ $# -eq 1 ]; then usage; fi; shift
			config_VerboseLevel $1 || usage
			;;

		# Aliases for "-v debug" and "-v nostats"
		--debug)
			config_VerboseLevel debug || usage
			;;
		--no-stats)
			config_VerboseLevel nostats || usage
			;;

		# Commands
		cron | fetch | upgrade | install | rollback | IDS)
			COMMANDS="${COMMANDS} $1"
			;;

		# Anything else is an error
		*)
			usage
			;;
		esac
		shift
	done

	# Make sure we have at least one command
	if [ -z "${COMMANDS}" ]; then
		usage
	fi
}

# Parse the configuration file
parse_conffile () {
	# If a configuration file was specified on the command line, check
	# that it exists and is readable.
	if [ ! -z "${CONFFILE}" ] && [ ! -r "${CONFFILE}" ]; then
		echo -n "File does not exist "
		echo -n "or is not readable: "
		echo ${CONFFILE}
		exit 1
	fi

	# If a configuration file was not specified on the command line,
	# use the default configuration file path.  If that default does
	# not exist, give up looking for any configuration.
	if [ -z "${CONFFILE}" ]; then
		CONFFILE="/etc/freebsd-update.conf"
		if [ ! -r "${CONFFILE}" ]; then
			return
		fi
	fi

	# Save the configuration options specified on the command line, and
	# clear all the options in preparation for reading the config file.
	saveconfig
	nullconfig

	# Read the configuration file.  Anything after the first '#' is
	# ignored, and any blank lines are ignored.
	L=0
	while read LINE; do
		L=$(($L + 1))
		LINEX=`echo "${LINE}" | cut -f 1 -d '#'`
		if ! configline ${LINEX}; then
			echo "Error processing configuration file, line $L:"
			echo "==> ${LINE}"
			exit 1
		fi
	done < ${CONFFILE}

	# Merge the settings read from the configuration file with those
	# provided at the command line.
	mergeconfig
}

# Provide some default parameters
default_params () {
	# Save any parameters already configured, and clear the slate
	saveconfig
	nullconfig

	# Default configurations
	config_WorkDir /var/db/freebsd-update
	config_MailTo root
	config_AllowAdd yes
	config_AllowDelete yes
	config_KeepModifiedMetadata yes
	config_BaseDir /
	config_VerboseLevel stats
	config_StrictComponents no
	config_BackupKernel yes
	config_BackupKernelDir /boot/kernel.old
	config_BackupKernelSymbolFiles no

	# Merge these defaults into the earlier-configured settings
	mergeconfig
}

# Set utility output filtering options, based on ${VERBOSELEVEL}
fetch_setup_verboselevel () {
	case ${VERBOSELEVEL} in
	debug)
		QUIETREDIR="/dev/stderr"
		QUIETFLAG=" "
		STATSREDIR="/dev/stderr"
		DDSTATS=".."
		XARGST="-t"
		NDEBUG=" "
		;;
	nostats)
		QUIETREDIR=""
		QUIETFLAG=""
		STATSREDIR="/dev/null"
		DDSTATS=".."
		XARGST=""
		NDEBUG=""
		;;
	stats)
		QUIETREDIR="/dev/null"
		QUIETFLAG="-q"
		STATSREDIR="/dev/stdout"
		DDSTATS=""
		XARGST=""
		NDEBUG="-n"
		;;
	esac
}

# Perform sanity checks and set some final parameters
# in preparation for fetching files.  Figure out which
# set of updates should be downloaded: If the user is
# running *-p[0-9]+, strip off the last part; if the
# user is running -SECURITY, call it -RELEASE.  Chdir
# into the working directory.
fetchupgrade_check_params () {
	export HTTP_USER_AGENT="freebsd-update (${COMMAND}, `uname -r`)"

	_SERVERNAME_z=\
"SERVERNAME must be given via command line or configuration file."
	_KEYPRINT_z="Key must be given via -k option or configuration file."
	_KEYPRINT_bad="Invalid key fingerprint: "
	_WORKDIR_bad="Directory does not exist or is not writable: "
	_WORKDIR_bad2="Directory is not on a persistent filesystem: "

	if [ -z "${SERVERNAME}" ]; then
		echo -n "`basename $0`: "
		echo "${_SERVERNAME_z}"
		exit 1
	fi
	if [ -z "${KEYPRINT}" ]; then
		echo -n "`basename $0`: "
		echo "${_KEYPRINT_z}"
		exit 1
	fi
	if ! echo "${KEYPRINT}" | grep -qE "^[0-9a-f]{64}$"; then
		echo -n "`basename $0`: "
		echo -n "${_KEYPRINT_bad}"
		echo ${KEYPRINT}
		exit 1
	fi
	if ! [ -d "${WORKDIR}" -a -w "${WORKDIR}" ]; then
		echo -n "`basename $0`: "
		echo -n "${_WORKDIR_bad}"
		echo ${WORKDIR}
		exit 1
	fi
	case `df -T ${WORKDIR}` in */dev/md[0-9]* | *tmpfs*)
		echo -n "`basename $0`: "
		echo -n "${_WORKDIR_bad2}"
		echo ${WORKDIR}
		exit 1
		;;
	esac
	chmod 700 ${WORKDIR}
	cd ${WORKDIR} || exit 1

	# Generate release number.  The s/SECURITY/RELEASE/ bit exists
	# to provide an upgrade path for FreeBSD Update 1.x users, since
	# the kernels provided by FreeBSD Update 1.x are always labelled
	# as X.Y-SECURITY.
	RELNUM=`uname -r |
	    sed -E 's,-p[0-9]+,,' |
	    sed -E 's,-SECURITY,-RELEASE,'`
	ARCH=`uname -m`
	FETCHDIR=${RELNUM}/${ARCH}
	PATCHDIR=${RELNUM}/${ARCH}/bp

	# Disallow upgrade from a version that is not a release
	case ${RELNUM} in
	*-RELEASE | *-ALPHA*  | *-BETA* | *-RC*)
		;;
	*)
		echo -n "`basename $0`: "
		cat <<- EOF
			Cannot upgrade from a version that is not a release
			(including alpha, beta and release candidates)
			using `basename $0`. Instead, FreeBSD can be directly
			upgraded by source or upgraded to a RELEASE/RELENG version
			prior to running `basename $0`.
			Currently running: ${RELNUM}
		EOF
		exit 1
		;;
	esac

	# Figure out what directory contains the running kernel
	BOOTFILE=`sysctl -n kern.bootfile`
	KERNELDIR=${BOOTFILE%/kernel}
	if ! [ -d ${KERNELDIR} ]; then
		echo "Cannot identify running kernel"
		exit 1
	fi

	# Figure out what kernel configuration is running.  We start with
	# the output of `uname -i`, and then make the following adjustments:
	# 1. Replace "SMP-GENERIC" with "SMP".  Why the SMP kernel config
	# file says "ident SMP-GENERIC", I don't know...
	# 2. If the kernel claims to be GENERIC _and_ ${ARCH} is "amd64"
	# _and_ `sysctl kern.version` contains a line which ends "/SMP", then
	# we're running an SMP kernel.  This mis-identification is a bug
	# which was fixed in 6.2-STABLE.
	KERNCONF=`uname -i`
	if [ ${KERNCONF} = "SMP-GENERIC" ]; then
		KERNCONF=SMP
	fi
	if [ ${KERNCONF} = "GENERIC" ] && [ ${ARCH} = "amd64" ]; then
		if sysctl kern.version | grep -qE '/SMP$'; then
			KERNCONF=SMP
		fi
	fi

	# Define some paths
	BSPATCH=/usr/bin/bspatch
	SHA256=/sbin/sha256
	PHTTPGET=/usr/libexec/phttpget

	# Set up variables relating to VERBOSELEVEL
	fetch_setup_verboselevel

	# Construct a unique name from ${BASEDIR}
	BDHASH=`echo ${BASEDIR} | sha256 -q`
}

# Perform sanity checks etc. before fetching updates.
fetch_check_params () {
	fetchupgrade_check_params

	if ! [ -z "${TARGETRELEASE}" ]; then
		echo -n "`basename $0`: "
		echo -n "-r option is meaningless with 'fetch' command.  "
		echo "(Did you mean 'upgrade' instead?)"
		exit 1
	fi

	# Check that we have updates ready to install
	if [ -f ${BDHASH}-install/kerneldone -a $FORCEFETCH -eq 0 ]; then
		echo "You have a partially completed upgrade pending"
		echo "Run '$0 install' first."
		echo "Run '$0 fetch -F' to proceed anyway."
		exit 1
	fi
}

# Perform sanity checks etc. before fetching upgrades.
upgrade_check_params () {
	fetchupgrade_check_params

	# Unless set otherwise, we're upgrading to the same kernel config.
	NKERNCONF=${KERNCONF}

	# We need TARGETRELEASE set
	_TARGETRELEASE_z="Release target must be specified via -r option."
	if [ -z "${TARGETRELEASE}" ]; then
		echo -n "`basename $0`: "
		echo "${_TARGETRELEASE_z}"
		exit 1
	fi

	# The target release should be != the current release.
	if [ "${TARGETRELEASE}" = "${RELNUM}" ]; then
		echo -n "`basename $0`: "
		echo "Cannot upgrade from ${RELNUM} to itself"
		exit 1
	fi

	# Turning off AllowAdd or AllowDelete is a bad idea for upgrades.
	if [ "${ALLOWADD}" = "no" ]; then
		echo -n "`basename $0`: "
		echo -n "WARNING: \"AllowAdd no\" is a bad idea "
		echo "when upgrading between releases."
		echo
	fi
	if [ "${ALLOWDELETE}" = "no" ]; then
		echo -n "`basename $0`: "
		echo -n "WARNING: \"AllowDelete no\" is a bad idea "
		echo "when upgrading between releases."
		echo
	fi

	# Set EDITOR to /usr/bin/vi if it isn't already set
	: ${EDITOR:='/usr/bin/vi'}
}

# Perform sanity checks and set some final parameters in
# preparation for installing updates.
install_check_params () {
	# Check that we are root.  All sorts of things won't work otherwise.
	if [ `id -u` != 0 ]; then
		echo "You must be root to run this."
		exit 1
	fi

	# Check that securelevel <= 0.  Otherwise we can't update schg files.
	if [ `sysctl -n kern.securelevel` -gt 0 ]; then
		echo "Updates cannot be installed when the system securelevel"
		echo "is greater than zero."
		exit 1
	fi

	# Check that we have a working directory
	_WORKDIR_bad="Directory does not exist or is not writable: "
	if ! [ -d "${WORKDIR}" -a -w "${WORKDIR}" ]; then
		echo -n "`basename $0`: "
		echo -n "${_WORKDIR_bad}"
		echo ${WORKDIR}
		exit 1
	fi
	cd ${WORKDIR} || exit 1

	# Construct a unique name from ${BASEDIR}
	BDHASH=`echo ${BASEDIR} | sha256 -q`

	# Check that we have updates ready to install
	if ! [ -L ${BDHASH}-install ]; then
		echo "No updates are available to install."
		if [ $ISFETCHED -eq 0 ]; then
			echo "Run '$0 fetch' first."
			exit 1
		fi
		exit 0
	fi
	if ! [ -f ${BDHASH}-install/INDEX-OLD ] ||
	    ! [ -f ${BDHASH}-install/INDEX-NEW ]; then
		echo "Update manifest is corrupt -- this should never happen."
		echo "Re-run '$0 fetch'."
		exit 1
	fi

	# Figure out what directory contains the running kernel
	BOOTFILE=`sysctl -n kern.bootfile`
	KERNELDIR=${BOOTFILE%/kernel}
	if ! [ -d ${KERNELDIR} ]; then
		echo "Cannot identify running kernel"
		exit 1
	fi
}

# Perform sanity checks and set some final parameters in
# preparation for UNinstalling updates.
rollback_check_params () {
	# Check that we are root.  All sorts of things won't work otherwise.
	if [ `id -u` != 0 ]; then
		echo "You must be root to run this."
		exit 1
	fi

	# Check that we have a working directory
	_WORKDIR_bad="Directory does not exist or is not writable: "
	if ! [ -d "${WORKDIR}" -a -w "${WORKDIR}" ]; then
		echo -n "`basename $0`: "
		echo -n "${_WORKDIR_bad}"
		echo ${WORKDIR}
		exit 1
	fi
	cd ${WORKDIR} || exit 1

	# Construct a unique name from ${BASEDIR}
	BDHASH=`echo ${BASEDIR} | sha256 -q`

	# Check that we have updates ready to rollback
	if ! [ -L ${BDHASH}-rollback ]; then
		echo "No rollback directory found."
		exit 1
	fi
	if ! [ -f ${BDHASH}-rollback/INDEX-OLD ] ||
	    ! [ -f ${BDHASH}-rollback/INDEX-NEW ]; then
		echo "Update manifest is corrupt -- this should never happen."
		exit 1
	fi
}

# Perform sanity checks and set some final parameters
# in preparation for comparing the system against the
# published index.  Figure out which index we should
# compare against: If the user is running *-p[0-9]+,
# strip off the last part; if the user is running
# -SECURITY, call it -RELEASE.  Chdir into the working
# directory.
IDS_check_params () {
	export HTTP_USER_AGENT="freebsd-update (${COMMAND}, `uname -r`)"

	_SERVERNAME_z=\
"SERVERNAME must be given via command line or configuration file."
	_KEYPRINT_z="Key must be given via -k option or configuration file."
	_KEYPRINT_bad="Invalid key fingerprint: "
	_WORKDIR_bad="Directory does not exist or is not writable: "

	if [ -z "${SERVERNAME}" ]; then
		echo -n "`basename $0`: "
		echo "${_SERVERNAME_z}"
		exit 1
	fi
	if [ -z "${KEYPRINT}" ]; then
		echo -n "`basename $0`: "
		echo "${_KEYPRINT_z}"
		exit 1
	fi
	if ! echo "${KEYPRINT}" | grep -qE "^[0-9a-f]{64}$"; then
		echo -n "`basename $0`: "
		echo -n "${_KEYPRINT_bad}"
		echo ${KEYPRINT}
		exit 1
	fi
	if ! [ -d "${WORKDIR}" -a -w "${WORKDIR}" ]; then
		echo -n "`basename $0`: "
		echo -n "${_WORKDIR_bad}"
		echo ${WORKDIR}
		exit 1
	fi
	cd ${WORKDIR} || exit 1

	# Generate release number.  The s/SECURITY/RELEASE/ bit exists
	# to provide an upgrade path for FreeBSD Update 1.x users, since
	# the kernels provided by FreeBSD Update 1.x are always labelled
	# as X.Y-SECURITY.
	RELNUM=`uname -r |
	    sed -E 's,-p[0-9]+,,' |
	    sed -E 's,-SECURITY,-RELEASE,'`
	ARCH=`uname -m`
	FETCHDIR=${RELNUM}/${ARCH}
	PATCHDIR=${RELNUM}/${ARCH}/bp

	# Figure out what directory contains the running kernel
	BOOTFILE=`sysctl -n kern.bootfile`
	KERNELDIR=${BOOTFILE%/kernel}
	if ! [ -d ${KERNELDIR} ]; then
		echo "Cannot identify running kernel"
		exit 1
	fi

	# Figure out what kernel configuration is running.  We start with
	# the output of `uname -i`, and then make the following adjustments:
	# 1. Replace "SMP-GENERIC" with "SMP".  Why the SMP kernel config
	# file says "ident SMP-GENERIC", I don't know...
	# 2. If the kernel claims to be GENERIC _and_ ${ARCH} is "amd64"
	# _and_ `sysctl kern.version` contains a line which ends "/SMP", then
	# we're running an SMP kernel.  This mis-identification is a bug
	# which was fixed in 6.2-STABLE.
	KERNCONF=`uname -i`
	if [ ${KERNCONF} = "SMP-GENERIC" ]; then
		KERNCONF=SMP
	fi
	if [ ${KERNCONF} = "GENERIC" ] && [ ${ARCH} = "amd64" ]; then
		if sysctl kern.version | grep -qE '/SMP$'; then
			KERNCONF=SMP
		fi
	fi

	# Define some paths
	SHA256=/sbin/sha256
	PHTTPGET=/usr/libexec/phttpget

	# Set up variables relating to VERBOSELEVEL
	fetch_setup_verboselevel
}

#### Core functionality -- the actual work gets done here

# Use an SRV query to pick a server.  If the SRV query doesn't provide
# a useful answer, use the server name specified by the user.
# Put another way... look up _http._tcp.${SERVERNAME} and pick a server
# from that; or if no servers are returned, use ${SERVERNAME}.
# This allows a user to specify "portsnap.freebsd.org" (in which case
# portsnap will select one of the mirrors) or "portsnap5.tld.freebsd.org"
# (in which case portsnap will use that particular server, since there
# won't be an SRV entry for that name).
#
# We ignore the Port field, since we are always going to use port 80.

# Fetch the mirror list, but do not pick a mirror yet.  Returns 1 if
# no mirrors are available for any reason.
fetch_pick_server_init () {
	: > serverlist_tried

# Check that host(1) exists (i.e., that the system wasn't built with the
# WITHOUT_BIND set) and don't try to find a mirror if it doesn't exist.
	if ! which -s host; then
		: > serverlist_full
		return 1
	fi

	echo -n "Looking up ${SERVERNAME} mirrors... "

# Issue the SRV query and pull out the Priority, Weight, and Target fields.
# BIND 9 prints "$name has SRV record ..." while BIND 8 prints
# "$name server selection ..."; we allow either format.
	MLIST="_http._tcp.${SERVERNAME}"
	host -t srv "${MLIST}" |
	    sed -nE "s/${MLIST} (has SRV record|server selection) //Ip" |
	    cut -f 1,2,4 -d ' ' |
	    sed -e 's/\.$//' |
	    sort > serverlist_full

# If no records, give up -- we'll just use the server name we were given.
	if [ `wc -l < serverlist_full` -eq 0 ]; then
		echo "none found."
		return 1
	fi

# Report how many mirrors we found.
	echo `wc -l < serverlist_full` "mirrors found."

# Generate a random seed for use in picking mirrors.  If HTTP_PROXY
# is set, this will be used to generate the seed; otherwise, the seed
# will be random.
	if [ -n "${HTTP_PROXY}${http_proxy}" ]; then
		RANDVALUE=`sha256 -qs "${HTTP_PROXY}${http_proxy}" |
		    tr -d 'a-f' |
		    cut -c 1-9`
	else
		RANDVALUE=`jot -r 1 0 999999999`
	fi
}

# Pick a mirror.  Returns 1 if we have run out of mirrors to try.
fetch_pick_server () {
# Generate a list of not-yet-tried mirrors
	sort serverlist_tried |
	    comm -23 serverlist_full - > serverlist

# Have we run out of mirrors?
	if [ `wc -l < serverlist` -eq 0 ]; then
		cat <<- EOF
			No mirrors remaining, giving up.

			This may be because upgrading from this platform (${ARCH})
			or release (${RELNUM}) is unsupported by `basename $0`. Only
			platforms with Tier 1 support can be upgraded by `basename $0`.
			See https://www.freebsd.org/platforms/index.html for more info.

			If unsupported, FreeBSD must be upgraded by source.
		EOF
		return 1
	fi

# Find the highest priority level (lowest numeric value).
	SRV_PRIORITY=`cut -f 1 -d ' ' serverlist | sort -n | head -1`

# Add up the weights of the response lines at that priority level.
	SRV_WSUM=0;
	while read X; do
		case "$X" in
		${SRV_PRIORITY}\ *)
			SRV_W=`echo $X | cut -f 2 -d ' '`
			SRV_WSUM=$(($SRV_WSUM + $SRV_W))
			;;
		esac
	done < serverlist

# If all the weights are 0, pretend that they are all 1 instead.
	if [ ${SRV_WSUM} -eq 0 ]; then
		SRV_WSUM=`grep -E "^${SRV_PRIORITY} " serverlist | wc -l`
		SRV_W_ADD=1
	else
		SRV_W_ADD=0
	fi

# Pick a value between 0 and the sum of the weights - 1
	SRV_RND=`expr ${RANDVALUE} % ${SRV_WSUM}`

# Read through the list of mirrors and set SERVERNAME.  Write the line
# corresponding to the mirror we selected into serverlist_tried so that
# we won't try it again.
	while read X; do
		case "$X" in
		${SRV_PRIORITY}\ *)
			SRV_W=`echo $X | cut -f 2 -d ' '`
			SRV_W=$(($SRV_W + $SRV_W_ADD))
			if [ $SRV_RND -lt $SRV_W ]; then
				SERVERNAME=`echo $X | cut -f 3 -d ' '`
				echo "$X" >> serverlist_tried
				break
			else
				SRV_RND=$(($SRV_RND - $SRV_W))
			fi
			;;
		esac
	done < serverlist
}

# Take a list of ${oldhash}|${newhash} and output a list of needed patches,
# i.e., those for which we have ${oldhash} and don't have ${newhash}.
fetch_make_patchlist () {
	grep -vE "^([0-9a-f]{64})\|\1$" |
	    tr '|' ' ' |
		while read X Y; do
			if [ -f "files/${Y}.gz" ] ||
			    [ ! -f "files/${X}.gz" ]; then
				continue
			fi
			echo "${X}|${Y}"
		done | sort -u
}

# Print user-friendly progress statistics
fetch_progress () {
	LNC=0
	while read x; do
		LNC=$(($LNC + 1))
		if [ $(($LNC % 10)) = 0 ]; then
			echo -n $LNC
		elif [ $(($LNC % 2)) = 0 ]; then
			echo -n .
		fi
	done
	echo -n " "
}

# Function for asking the user if everything is ok
continuep () {
	while read -p "Does this look reasonable (y/n)? " CONTINUE; do
		case "${CONTINUE}" in
		y*)
			return 0
			;;
		n*)
			return 1
			;;
		esac
	done
}

# Initialize the working directory
workdir_init () {
	mkdir -p files
	touch tINDEX.present
}

# Check that we have a public key with an appropriate hash, or
# fetch the key if it doesn't exist.  Returns 1 if the key has
# not yet been fetched.
fetch_key () {
	if [ -r pub.ssl ] && [ `${SHA256} -q pub.ssl` = ${KEYPRINT} ]; then
		return 0
	fi

	echo -n "Fetching public key from ${SERVERNAME}... "
	rm -f pub.ssl
	fetch ${QUIETFLAG} http://${SERVERNAME}/${FETCHDIR}/pub.ssl \
	    2>${QUIETREDIR} || true
	if ! [ -r pub.ssl ]; then
		echo "failed."
		return 1
	fi
	if ! [ `${SHA256} -q pub.ssl` = ${KEYPRINT} ]; then
		echo "key has incorrect hash."
		rm -f pub.ssl
		return 1
	fi
	echo "done."
}

# Fetch metadata signature, aka "tag".
fetch_tag () {
	echo -n "Fetching metadata signature "
	echo ${NDEBUG} "for ${RELNUM} from ${SERVERNAME}... "
	rm -f latest.ssl
	fetch ${QUIETFLAG} http://${SERVERNAME}/${FETCHDIR}/latest.ssl	\
	    2>${QUIETREDIR} || true
	if ! [ -r latest.ssl ]; then
		echo "failed."
		return 1
	fi

	openssl rsautl -pubin -inkey pub.ssl -verify		\
	    < latest.ssl > tag.new 2>${QUIETREDIR} || true
	rm latest.ssl

	if ! [ `wc -l < tag.new` = 1 ] ||
	    ! grep -qE	\
    "^freebsd-update\|${ARCH}\|${RELNUM}\|[0-9]+\|[0-9a-f]{64}\|[0-9]{10}" \
		tag.new; then
		echo "invalid signature."
		return 1
	fi

	echo "done."

	RELPATCHNUM=`cut -f 4 -d '|' < tag.new`
	TINDEXHASH=`cut -f 5 -d '|' < tag.new`
	EOLTIME=`cut -f 6 -d '|' < tag.new`
}

# Sanity-check the patch number in a tag, to make sure that we're not
# going to "update" backwards and to prevent replay attacks.
fetch_tagsanity () {
	# Check that we're not going to move from -pX to -pY with Y < X.
	RELPX=`uname -r | sed -E 's,.*-,,'`
	if echo ${RELPX} | grep -qE '^p[0-9]+$'; then
		RELPX=`echo ${RELPX} | cut -c 2-`
	else
		RELPX=0
	fi
	if [ "${RELPATCHNUM}" -lt "${RELPX}" ]; then
		echo
		echo -n "Files on mirror (${RELNUM}-p${RELPATCHNUM})"
		echo " appear older than what"
		echo "we are currently running (`uname -r`)!"
		echo "Cowardly refusing to proceed any further."
		return 1
	fi

	# If "tag" exists and corresponds to ${RELNUM}, make sure that
	# it contains a patch number <= RELPATCHNUM, in order to protect
	# against rollback (replay) attacks.
	if [ -f tag ] &&
	    grep -qE	\
    "^freebsd-update\|${ARCH}\|${RELNUM}\|[0-9]+\|[0-9a-f]{64}\|[0-9]{10}" \
		tag; then
		LASTRELPATCHNUM=`cut -f 4 -d '|' < tag`

		if [ "${RELPATCHNUM}" -lt "${LASTRELPATCHNUM}" ]; then
			echo
			echo -n "Files on mirror (${RELNUM}-p${RELPATCHNUM})"
			echo " are older than the"
			echo -n "most recently seen updates"
			echo " (${RELNUM}-p${LASTRELPATCHNUM})."
			echo "Cowardly refusing to proceed any further."
			return 1
		fi
	fi
}

# Fetch metadata index file
fetch_metadata_index () {
	echo ${NDEBUG} "Fetching metadata index... "
	rm -f ${TINDEXHASH}
	fetch ${QUIETFLAG} http://${SERVERNAME}/${FETCHDIR}/t/${TINDEXHASH}
	    2>${QUIETREDIR}
	if ! [ -f ${TINDEXHASH} ]; then
		echo "failed."
		return 1
	fi
	if [ `${SHA256} -q ${TINDEXHASH}` != ${TINDEXHASH} ]; then
		echo "update metadata index corrupt."
		return 1
	fi
	echo "done."
}

# Print an error message about signed metadata being bogus.
fetch_metadata_bogus () {
	echo
	echo "The update metadata$1 is correctly signed, but"
	echo "failed an integrity check."
	echo "Cowardly refusing to proceed any further."
	return 1
}

# Construct tINDEX.new by merging the lines named in $1 from ${TINDEXHASH}
# with the lines not named in $@ from tINDEX.present (if that file exists).
fetch_metadata_index_merge () {
	for METAFILE in $@; do
		if [ `grep -E "^${METAFILE}\|" ${TINDEXHASH} | wc -l`	\
		    -ne 1 ]; then
			fetch_metadata_bogus " index"
			return 1
		fi

		grep -E "${METAFILE}\|" ${TINDEXHASH}
	done |
	    sort > tINDEX.wanted

	if [ -f tINDEX.present ]; then
		join -t '|' -v 2 tINDEX.wanted tINDEX.present |
		    sort -m - tINDEX.wanted > tINDEX.new
		rm tINDEX.wanted
	else
		mv tINDEX.wanted tINDEX.new
	fi
}

# Sanity check all the lines of tINDEX.new.  Even if more metadata lines
# are added by future versions of the server, this won't cause problems,
# since the only lines which appear in tINDEX.new are the ones which we
# specifically grepped out of ${TINDEXHASH}.
fetch_metadata_index_sanity () {
	if grep -qvE '^[0-9A-Z.-]+\|[0-9a-f]{64}$' tINDEX.new; then
		fetch_metadata_bogus " index"
		return 1
	fi
}

# Sanity check the metadata file $1.
fetch_metadata_sanity () {
	# Some aliases to save space later: ${P} is a character which can
	# appear in a path; ${M} is the four numeric metadata fields; and
	# ${H} is a sha256 hash.
	P="[-+./:=,%@_[~[:alnum:]]"
	M="[0-9]+\|[0-9]+\|[0-9]+\|[0-9]+"
	H="[0-9a-f]{64}"

	# Check that the first four fields make sense.
	if gunzip -c < files/$1.gz |
	    grep -qvE "^[a-z]+\|[0-9a-z-]+\|${P}+\|[fdL-]\|"; then
		fetch_metadata_bogus ""
		return 1
	fi

	# Remove the first three fields.
	gunzip -c < files/$1.gz |
	    cut -f 4- -d '|' > sanitycheck.tmp

	# Sanity check entries with type 'f'
	if grep -E '^f' sanitycheck.tmp |
	    grep -qvE "^f\|${M}\|${H}\|${P}*\$"; then
		fetch_metadata_bogus ""
		return 1
	fi

	# Sanity check entries with type 'd'
	if grep -E '^d' sanitycheck.tmp |
	    grep -qvE "^d\|${M}\|\|\$"; then
		fetch_metadata_bogus ""
		return 1
	fi

	# Sanity check entries with type 'L'
	if grep -E '^L' sanitycheck.tmp |
	    grep -qvE "^L\|${M}\|${P}*\|\$"; then
		fetch_metadata_bogus ""
		return 1
	fi

	# Sanity check entries with type '-'
	if grep -E '^-' sanitycheck.tmp |
	    grep -qvE "^-\|\|\|\|\|\|"; then
		fetch_metadata_bogus ""
		return 1
	fi

	# Clean up
	rm sanitycheck.tmp
}

# Fetch the metadata index and metadata files listed in $@,
# taking advantage of metadata patches where possible.
fetch_metadata () {
	fetch_metadata_index || return 1
	fetch_metadata_index_merge $@ || return 1
	fetch_metadata_index_sanity || return 1

	# Generate a list of wanted metadata patches
	join -t '|' -o 1.2,2.2 tINDEX.present tINDEX.new |
	    fetch_make_patchlist > patchlist

	if [ -s patchlist ]; then
		# Attempt to fetch metadata patches
		echo -n "Fetching `wc -l < patchlist | tr -d ' '` "
		echo ${NDEBUG} "metadata patches.${DDSTATS}"
		tr '|' '-' < patchlist |
		    lam -s "${FETCHDIR}/tp/" - -s ".gz" |
		    xargs ${XARGST} ${PHTTPGET} ${SERVERNAME}	\
			2>${STATSREDIR} | fetch_progress
		echo "done."

		# Attempt to apply metadata patches
		echo -n "Applying metadata patches... "
		tr '|' ' ' < patchlist |
		    while read X Y; do
			if [ ! -f "${X}-${Y}.gz" ]; then continue; fi
			gunzip -c < ${X}-${Y}.gz > diff
			gunzip -c < files/${X}.gz > diff-OLD

			# Figure out which lines are being added and removed
			grep -E '^-' diff |
			    cut -c 2- |
			    while read PREFIX; do
				look "${PREFIX}" diff-OLD
			    done |
			    sort > diff-rm
			grep -E '^\+' diff |
			    cut -c 2- > diff-add

			# Generate the new file
			comm -23 diff-OLD diff-rm |
			    sort - diff-add > diff-NEW

			if [ `${SHA256} -q diff-NEW` = ${Y} ]; then
				mv diff-NEW files/${Y}
				gzip -n files/${Y}
			else
				mv diff-NEW ${Y}.bad
			fi
			rm -f ${X}-${Y}.gz diff
			rm -f diff-OLD diff-NEW diff-add diff-rm
		done 2>${QUIETREDIR}
		echo "done."
	fi

	# Update metadata without patches
	cut -f 2 -d '|' < tINDEX.new |
	    while read Y; do
		if [ ! -f "files/${Y}.gz" ]; then
			echo ${Y};
		fi
	    done |
	    sort -u > filelist

	if [ -s filelist ]; then
		echo -n "Fetching `wc -l < filelist | tr -d ' '` "
		echo ${NDEBUG} "metadata files... "
		lam -s "${FETCHDIR}/m/" - -s ".gz" < filelist |
		    xargs ${XARGST} ${PHTTPGET} ${SERVERNAME}	\
		    2>${QUIETREDIR}

		while read Y; do
			if ! [ -f ${Y}.gz ]; then
				echo "failed."
				return 1
			fi
			if [ `gunzip -c < ${Y}.gz |
			    ${SHA256} -q` = ${Y} ]; then
				mv ${Y}.gz files/${Y}.gz
			else
				echo "metadata is corrupt."
				return 1
			fi
		done < filelist
		echo "done."
	fi

# Sanity-check the metadata files.
	cut -f 2 -d '|' tINDEX.new > filelist
	while read X; do
		fetch_metadata_sanity ${X} || return 1
	done < filelist

# Remove files which are no longer needed
	cut -f 2 -d '|' tINDEX.present |
	    sort > oldfiles
	cut -f 2 -d '|' tINDEX.new |
	    sort |
	    comm -13 - oldfiles |
	    lam -s "files/" - -s ".gz" |
	    xargs rm -f
	rm patchlist filelist oldfiles
	rm ${TINDEXHASH}

# We're done!
	mv tINDEX.new tINDEX.present
	mv tag.new tag

	return 0
}

# Extract a subset of a downloaded metadata file containing only the parts
# which are listed in COMPONENTS.
fetch_filter_metadata_components () {
	METAHASH=`look "$1|" tINDEX.present | cut -f 2 -d '|'`
	gunzip -c < files/${METAHASH}.gz > $1.all

	# Fish out the lines belonging to components we care about.
	for C in ${COMPONENTS}; do
		look "`echo ${C} | tr '/' '|'`|" $1.all
	done > $1

	# Remove temporary file.
	rm $1.all
}

# Generate a filtered version of the metadata file $1 from the downloaded
# file, by fishing out the lines corresponding to components we're trying
# to keep updated, and then removing lines corresponding to paths we want
# to ignore.
fetch_filter_metadata () {
	# Fish out the lines belonging to components we care about.
	fetch_filter_metadata_components $1

	# Canonicalize directory names by removing any trailing / in
	# order to avoid listing directories multiple times if they
	# belong to multiple components.  Turning "/" into "" doesn't
	# matter, since we add a leading "/" when we use paths later.
	cut -f 3- -d '|' $1 |
	    sed -e 's,/|d|,|d|,' |
	    sed -e 's,/|-|,|-|,' |
	    sort -u > $1.tmp

	# Figure out which lines to ignore and remove them.
	for X in ${IGNOREPATHS}; do
		grep -E "^${X}" $1.tmp
	done |
	    sort -u |
	    comm -13 - $1.tmp > $1

	# Remove temporary files.
	rm $1.tmp
}

# Filter the metadata file $1 by adding lines with "/boot/$2"
# replaced by ${KERNELDIR} (which is `sysctl -n kern.bootfile` minus the
# trailing "/kernel"); and if "/boot/$2" does not exist, remove
# the original lines which start with that.
# Put another way: Deal with the fact that the FOO kernel is sometimes
# installed in /boot/FOO/ and is sometimes installed elsewhere.
fetch_filter_kernel_names () {
	grep ^/boot/$2 $1 |
	    sed -e "s,/boot/$2,${KERNELDIR},g" |
	    sort - $1 > $1.tmp
	mv $1.tmp $1

	if ! [ -d /boot/$2 ]; then
		grep -v ^/boot/$2 $1 > $1.tmp
		mv $1.tmp $1
	fi
}

# For all paths appearing in $1 or $3, inspect the system
# and generate $2 describing what is currently installed.
fetch_inspect_system () {
	# No errors yet...
	rm -f .err

	# Tell the user why his disk is suddenly making lots of noise
	echo -n "Inspecting system... "

	# Generate list of files to inspect
	cat $1 $3 |
	    cut -f 1 -d '|' |
	    sort -u > filelist

	# Examine each file and output lines of the form
	# /path/to/file|type|device-inum|user|group|perm|flags|value
	# sorted by device and inode number.
	while read F; do
		# If the symlink/file/directory does not exist, record this.
		if ! [ -e ${BASEDIR}/${F} ]; then
			echo "${F}|-||||||"
			continue
		fi
		if ! [ -r ${BASEDIR}/${F} ]; then
			echo "Cannot read file: ${BASEDIR}/${F}"	\
			    >/dev/stderr
			touch .err
			return 1
		fi

		# Otherwise, output an index line.
		if [ -L ${BASEDIR}/${F} ]; then
			echo -n "${F}|L|"
			stat -n -f '%d-%i|%u|%g|%Mp%Lp|%Of|' ${BASEDIR}/${F};
			readlink ${BASEDIR}/${F};
		elif [ -f ${BASEDIR}/${F} ]; then
			echo -n "${F}|f|"
			stat -n -f '%d-%i|%u|%g|%Mp%Lp|%Of|' ${BASEDIR}/${F};
			sha256 -q ${BASEDIR}/${F};
		elif [ -d ${BASEDIR}/${F} ]; then
			echo -n "${F}|d|"
			stat -f '%d-%i|%u|%g|%Mp%Lp|%Of|' ${BASEDIR}/${F};
		else
			echo "Unknown file type: ${BASEDIR}/${F}"	\
			    >/dev/stderr
			touch .err
			return 1
		fi
	done < filelist |
	    sort -k 3,3 -t '|' > $2.tmp
	rm filelist

	# Check if an error occurred during system inspection
	if [ -f .err ]; then
		return 1
	fi

	# Convert to the form
	# /path/to/file|type|user|group|perm|flags|value|hlink
	# by resolving identical device and inode numbers into hard links.
	cut -f 1,3 -d '|' $2.tmp |
	    sort -k 1,1 -t '|' |
	    sort -s -u -k 2,2 -t '|' |
	    join -1 2 -2 3 -t '|' - $2.tmp |
	    awk -F \| -v OFS=\|		\
		'{
		    if (($2 == $3) || ($4 == "-"))
			print $3,$4,$5,$6,$7,$8,$9,""
		    else
			print $3,$4,$5,$6,$7,$8,$9,$2
		}' |
	    sort > $2
	rm $2.tmp

	# We're finished looking around
	echo "done."
}

# For any paths matching ${MERGECHANGES}, compare $1 and $2 and find any
# files which differ; generate $3 containing these paths and the old hashes.
fetch_filter_mergechanges () {
	# Pull out the paths and hashes of the files matching ${MERGECHANGES}.
	for F in $1 $2; do
		for X in ${MERGECHANGES}; do
			grep -E "^${X}" ${F}
		done |
		    cut -f 1,2,7 -d '|' |
		    sort > ${F}-values
	done

	# Any line in $2-values which doesn't appear in $1-values and is a
	# file means that we should list the path in $3.
	comm -13 $1-values $2-values |
	    fgrep '|f|' |
	    cut -f 1 -d '|' > $2-paths

	# For each path, pull out one (and only one!) entry from $1-values.
	# Note that we cannot distinguish which "old" version the user made
	# changes to; but hopefully any changes which occur due to security
	# updates will exist in both the "new" version and the version which
	# the user has installed, so the merging will still work.
	while read X; do
		look "${X}|" $1-values |
		    head -1
	done < $2-paths > $3

	# Clean up
	rm $1-values $2-values $2-paths
}

# For any paths matching ${UPDATEIFUNMODIFIED}, remove lines from $[123]
# which correspond to lines in $2 with hashes not matching $1 or $3, unless
# the paths are listed in $4.  For entries in $2 marked "not present"
# (aka. type -), remove lines from $[123] unless there is a corresponding
# entry in $1.
fetch_filter_unmodified_notpresent () {
	# Figure out which lines of $1 and $3 correspond to bits which
	# should only be updated if they haven't changed, and fish out
	# the (path, type, value) tuples.
	# NOTE: We don't consider a file to be "modified" if it matches
	# the hash from $3.
	for X in ${UPDATEIFUNMODIFIED}; do
		grep -E "^${X}" $1
		grep -E "^${X}" $3
	done |
	    cut -f 1,2,7 -d '|' |
	    sort > $1-values

	# Do the same for $2.
	for X in ${UPDATEIFUNMODIFIED}; do
		grep -E "^${X}" $2
	done |
	    cut -f 1,2,7 -d '|' |
	    sort > $2-values

	# Any entry in $2-values which is not in $1-values corresponds to
	# a path which we need to remove from $1, $2, and $3, unless it
	# that path appears in $4.
	comm -13 $1-values $2-values |
	    sort -t '|' -k 1,1 > mlines.tmp
	cut -f 1 -d '|' $4 |
	    sort |
	    join -v 2 -t '|' - mlines.tmp |
	    sort > mlines
	rm $1-values $2-values mlines.tmp

	# Any lines in $2 which are not in $1 AND are "not present" lines
	# also belong in mlines.
	comm -13 $1 $2 |
	    cut -f 1,2,7 -d '|' |
	    fgrep '|-|' >> mlines

	# Remove lines from $1, $2, and $3
	for X in $1 $2 $3; do
		sort -t '|' -k 1,1 ${X} > ${X}.tmp
		cut -f 1 -d '|' < mlines |
		    sort |
		    join -v 2 -t '|' - ${X}.tmp |
		    sort > ${X}
		rm ${X}.tmp
	done

	# Store a list of the modified files, for future reference
	fgrep -v '|-|' mlines |
	    cut -f 1 -d '|' > modifiedfiles
	rm mlines
}

# For each entry in $1 of type -, remove any corresponding
# entry from $2 if ${ALLOWADD} != "yes".  Remove all entries
# of type - from $1.
fetch_filter_allowadd () {
	cut -f 1,2 -d '|' < $1 |
	    fgrep '|-' |
	    cut -f 1 -d '|' > filesnotpresent

	if [ ${ALLOWADD} != "yes" ]; then
		sort < $2 |
		    join -v 1 -t '|' - filesnotpresent |
		    sort > $2.tmp
		mv $2.tmp $2
	fi

	sort < $1 |
	    join -v 1 -t '|' - filesnotpresent |
	    sort > $1.tmp
	mv $1.tmp $1
	rm filesnotpresent
}

# If ${ALLOWDELETE} != "yes", then remove any entries from $1
# which don't correspond to entries in $2.
fetch_filter_allowdelete () {
	# Produce a lists ${PATH}|${TYPE}
	for X in $1 $2; do
		cut -f 1-2 -d '|' < ${X} |
		    sort -u > ${X}.nodes
	done

	# Figure out which lines need to be removed from $1.
	if [ ${ALLOWDELETE} != "yes" ]; then
		comm -23 $1.nodes $2.nodes > $1.badnodes
	else
		: > $1.badnodes
	fi

	# Remove the relevant lines from $1
	while read X; do
		look "${X}|" $1
	done < $1.badnodes |
	    comm -13 - $1 > $1.tmp
	mv $1.tmp $1

	rm $1.badnodes $1.nodes $2.nodes
}

# If ${KEEPMODIFIEDMETADATA} == "yes", then for each entry in $2
# with metadata not matching any entry in $1, replace the corresponding
# line of $3 with one having the same metadata as the entry in $2.
fetch_filter_modified_metadata () {
	# Fish out the metadata from $1 and $2
	for X in $1 $2; do
		cut -f 1-6 -d '|' < ${X} > ${X}.metadata
	done

	# Find the metadata we need to keep
	if [ ${KEEPMODIFIEDMETADATA} = "yes" ]; then
		comm -13 $1.metadata $2.metadata > keepmeta
	else
		: > keepmeta
	fi

	# Extract the lines which we need to remove from $3, and
	# construct the lines which we need to add to $3.
	: > $3.remove
	: > $3.add
	while read LINE; do
		NODE=`echo "${LINE}" | cut -f 1-2 -d '|'`
		look "${NODE}|" $3 >> $3.remove
		look "${NODE}|" $3 |
		    cut -f 7- -d '|' |
		    lam -s "${LINE}|" - >> $3.add
	done < keepmeta

	# Remove the specified lines and add the new lines.
	sort $3.remove |
	    comm -13 - $3 |
	    sort -u - $3.add > $3.tmp
	mv $3.tmp $3

	rm keepmeta $1.metadata $2.metadata $3.add $3.remove
}

# Remove lines from $1 and $2 which are identical;
# no need to update a file if it isn't changing.
fetch_filter_uptodate () {
	comm -23 $1 $2 > $1.tmp
	comm -13 $1 $2 > $2.tmp

	mv $1.tmp $1
	mv $2.tmp $2
}

# Fetch any "clean" old versions of files we need for merging changes.
fetch_files_premerge () {
	# We only need to do anything if $1 is non-empty.
	if [ -s $1 ]; then
		# Tell the user what we're doing
		echo -n "Fetching files from ${OLDRELNUM} for merging... "

		# List of files wanted
		fgrep '|f|' < $1 |
		    cut -f 3 -d '|' |
		    sort -u > files.wanted

		# Only fetch the files we don't already have
		while read Y; do
			if [ ! -f "files/${Y}.gz" ]; then
				echo ${Y};
			fi
		done < files.wanted > filelist

		# Actually fetch them
		lam -s "${OLDFETCHDIR}/f/" - -s ".gz" < filelist |
		    xargs ${XARGST} ${PHTTPGET} ${SERVERNAME}	\
		    2>${QUIETREDIR}

		# Make sure we got them all, and move them into /files/
		while read Y; do
			if ! [ -f ${Y}.gz ]; then
				echo "failed."
				return 1
			fi
			if [ `gunzip -c < ${Y}.gz |
			    ${SHA256} -q` = ${Y} ]; then
				mv ${Y}.gz files/${Y}.gz
			else
				echo "${Y} has incorrect hash."
				return 1
			fi
		done < filelist
		echo "done."

		# Clean up
		rm filelist files.wanted
	fi
}

# Prepare to fetch files: Generate a list of the files we need,
# copy the unmodified files we have into /files/, and generate
# a list of patches to download.
fetch_files_prepare () {
	# Tell the user why his disk is suddenly making lots of noise
	echo -n "Preparing to download files... "

	# Reduce indices to ${PATH}|${HASH} pairs
	for X in $1 $2 $3; do
		cut -f 1,2,7 -d '|' < ${X} |
		    fgrep '|f|' |
		    cut -f 1,3 -d '|' |
		    sort > ${X}.hashes
	done

	# List of files wanted
	cut -f 2 -d '|' < $3.hashes |
	    sort -u |
	    while read HASH; do
		if ! [ -f files/${HASH}.gz ]; then
			echo ${HASH}
		fi
	done > files.wanted

	# Generate a list of unmodified files
	comm -12 $1.hashes $2.hashes |
	    sort -k 1,1 -t '|' > unmodified.files

	# Copy all files into /files/.  We only need the unmodified files
	# for use in patching; but we'll want all of them if the user asks
	# to rollback the updates later.
	while read LINE; do
		F=`echo "${LINE}" | cut -f 1 -d '|'`
		HASH=`echo "${LINE}" | cut -f 2 -d '|'`

		# Skip files we already have.
		if [ -f files/${HASH}.gz ]; then
			continue
		fi

		# Make sure the file hasn't changed.
		cp "${BASEDIR}/${F}" tmpfile
		if [ `sha256 -q tmpfile` != ${HASH} ]; then
			echo
			echo "File changed while FreeBSD Update running: ${F}"
			return 1
		fi

		# Place the file into storage.
		gzip -c < tmpfile > files/${HASH}.gz
		rm tmpfile
	done < $2.hashes

	# Produce a list of patches to download
	sort -k 1,1 -t '|' $3.hashes |
	    join -t '|' -o 2.2,1.2 - unmodified.files |
	    fetch_make_patchlist > patchlist

	# Garbage collect
	rm unmodified.files $1.hashes $2.hashes $3.hashes

	# We don't need the list of possible old files any more.
	rm $1

	# We're finished making noise
	echo "done."
}

# Fetch files.
fetch_files () {
	# Attempt to fetch patches
	if [ -s patchlist ]; then
		echo -n "Fetching `wc -l < patchlist | tr -d ' '` "
		echo ${NDEBUG} "patches.${DDSTATS}"
		tr '|' '-' < patchlist |
		    lam -s "${PATCHDIR}/" - |
		    xargs ${XARGST} ${PHTTPGET} ${SERVERNAME}	\
			2>${STATSREDIR} | fetch_progress
		echo "done."

		# Attempt to apply patches
		echo -n "Applying patches... "
		tr '|' ' ' < patchlist |
		    while read X Y; do
			if [ ! -f "${X}-${Y}" ]; then continue; fi
			gunzip -c < files/${X}.gz > OLD

			bspatch OLD NEW ${X}-${Y}

			if [ `${SHA256} -q NEW` = ${Y} ]; then
				mv NEW files/${Y}
				gzip -n files/${Y}
			fi
			rm -f diff OLD NEW ${X}-${Y}
		done 2>${QUIETREDIR}
		echo "done."
	fi

	# Download files which couldn't be generate via patching
	while read Y; do
		if [ ! -f "files/${Y}.gz" ]; then
			echo ${Y};
		fi
	done < files.wanted > filelist

	if [ -s filelist ]; then
		echo -n "Fetching `wc -l < filelist | tr -d ' '` "
		echo ${NDEBUG} "files... "
		lam -s "${FETCHDIR}/f/" - -s ".gz" < filelist |
		    xargs ${XARGST} ${PHTTPGET} ${SERVERNAME}	\
			2>${STATSREDIR} | fetch_progress

		while read Y; do
			if ! [ -f ${Y}.gz ]; then
				echo "failed."
				return 1
			fi
			if [ `gunzip -c < ${Y}.gz |
			    ${SHA256} -q` = ${Y} ]; then
				mv ${Y}.gz files/${Y}.gz
			else
				echo "${Y} has incorrect hash."
				return 1
			fi
		done < filelist
		echo "done."
	fi

	# Clean up
	rm files.wanted filelist patchlist
}

# Create and populate install manifest directory; and report what updates
# are available.
fetch_create_manifest () {
	# If we have an existing install manifest, nuke it.
	if [ -L "${BDHASH}-install" ]; then
		rm -r ${BDHASH}-install/
		rm ${BDHASH}-install
	fi

	# Report to the user if any updates were avoided due to local changes
	if [ -s modifiedfiles ]; then
		cat - modifiedfiles <<- EOF | ${PAGER}
			The following files are affected by updates. No changes have
			been downloaded, however, because the files have been modified
			locally:
		EOF
	fi
	rm modifiedfiles

	# If no files will be updated, tell the user and exit
	if ! [ -s INDEX-PRESENT ] &&
	    ! [ -s INDEX-NEW ]; then
		rm INDEX-PRESENT INDEX-NEW
		echo
		echo -n "No updates needed to update system to "
		echo "${RELNUM}-p${RELPATCHNUM}."
		return
	fi

	# Divide files into (a) removed files, (b) added files, and
	# (c) updated files.
	cut -f 1 -d '|' < INDEX-PRESENT |
	    sort > INDEX-PRESENT.flist
	cut -f 1 -d '|' < INDEX-NEW |
	    sort > INDEX-NEW.flist
	comm -23 INDEX-PRESENT.flist INDEX-NEW.flist > files.removed
	comm -13 INDEX-PRESENT.flist INDEX-NEW.flist > files.added
	comm -12 INDEX-PRESENT.flist INDEX-NEW.flist > files.updated
	rm INDEX-PRESENT.flist INDEX-NEW.flist

	# Report removed files, if any
	if [ -s files.removed ]; then
		cat - files.removed <<- EOF | ${PAGER}
			The following files will be removed as part of updating to
			${RELNUM}-p${RELPATCHNUM}:
		EOF
	fi
	rm files.removed

	# Report added files, if any
	if [ -s files.added ]; then
		cat - files.added <<- EOF | ${PAGER}
			The following files will be added as part of updating to
			${RELNUM}-p${RELPATCHNUM}:
		EOF
	fi
	rm files.added

	# Report updated files, if any
	if [ -s files.updated ]; then
		cat - files.updated <<- EOF | ${PAGER}
			The following files will be updated as part of updating to
			${RELNUM}-p${RELPATCHNUM}:
		EOF
	fi
	rm files.updated

	# Create a directory for the install manifest.
	MDIR=`mktemp -d install.XXXXXX` || return 1

	# Populate it
	mv INDEX-PRESENT ${MDIR}/INDEX-OLD
	mv INDEX-NEW ${MDIR}/INDEX-NEW

	# Link it into place
	ln -s ${MDIR} ${BDHASH}-install
}

# Warn about any upcoming EoL
fetch_warn_eol () {
	# What's the current time?
	NOWTIME=`date "+%s"`

	# When did we last warn about the EoL date?
	if [ -f lasteolwarn ]; then
		LASTWARN=`cat lasteolwarn`
	else
		LASTWARN=`expr ${NOWTIME} - 63072000`
	fi

	# If the EoL time is past, warn.
	if [ ${EOLTIME} -lt ${NOWTIME} ]; then
		echo
		cat <<-EOF
		WARNING: `uname -sr` HAS PASSED ITS END-OF-LIFE DATE.
		Any security issues discovered after `date -r ${EOLTIME}`
		will not have been corrected.
		EOF
		return 1
	fi

	# Figure out how long it has been since we last warned about the
	# upcoming EoL, and how much longer we have left.
	SINCEWARN=`expr ${NOWTIME} - ${LASTWARN}`
	TIMELEFT=`expr ${EOLTIME} - ${NOWTIME}`

	# Don't warn if the EoL is more than 3 months away
	if [ ${TIMELEFT} -gt 7884000 ]; then
		return 0
	fi

	# Don't warn if the time remaining is more than 3 times the time
	# since the last warning.
	if [ ${TIMELEFT} -gt `expr ${SINCEWARN} \* 3` ]; then
		return 0
	fi

	# Figure out what time units to use.
	if [ ${TIMELEFT} -lt 604800 ]; then
		UNIT="day"
		SIZE=86400
	elif [ ${TIMELEFT} -lt 2678400 ]; then
		UNIT="week"
		SIZE=604800
	else
		UNIT="month"
		SIZE=2678400
	fi

	# Compute the right number of units
	NUM=`expr ${TIMELEFT} / ${SIZE}`
	if [ ${NUM} != 1 ]; then
		UNIT="${UNIT}s"
	fi

	# Print the warning
	echo
	cat <<-EOF
		WARNING: `uname -sr` is approaching its End-of-Life date.
		It is strongly recommended that you upgrade to a newer
		release within the next ${NUM} ${UNIT}.
	EOF

	# Update the stored time of last warning
	echo ${NOWTIME} > lasteolwarn
}

# Do the actual work involved in "fetch" / "cron".
fetch_run () {
	workdir_init || return 1

	# Prepare the mirror list.
	fetch_pick_server_init && fetch_pick_server

	# Try to fetch the public key until we run out of servers.
	while ! fetch_key; do
		fetch_pick_server || return 1
	done

	# Try to fetch the metadata index signature ("tag") until we run
	# out of available servers; and sanity check the downloaded tag.
	while ! fetch_tag; do
		fetch_pick_server || return 1
	done
	fetch_tagsanity || return 1

	# Fetch the latest INDEX-NEW and INDEX-OLD files.
	fetch_metadata INDEX-NEW INDEX-OLD || return 1

	# Generate filtered INDEX-NEW and INDEX-OLD files containing only
	# the lines which (a) belong to components we care about, and (b)
	# don't correspond to paths we're explicitly ignoring.
	fetch_filter_metadata INDEX-NEW || return 1
	fetch_filter_metadata INDEX-OLD || return 1

	# Translate /boot/${KERNCONF} into ${KERNELDIR}
	fetch_filter_kernel_names INDEX-NEW ${KERNCONF}
	fetch_filter_kernel_names INDEX-OLD ${KERNCONF}

	# For all paths appearing in INDEX-OLD or INDEX-NEW, inspect the
	# system and generate an INDEX-PRESENT file.
	fetch_inspect_system INDEX-OLD INDEX-PRESENT INDEX-NEW || return 1

	# Based on ${UPDATEIFUNMODIFIED}, remove lines from INDEX-* which
	# correspond to lines in INDEX-PRESENT with hashes not appearing
	# in INDEX-OLD or INDEX-NEW.  Also remove lines where the entry in
	# INDEX-PRESENT has type - and there isn't a corresponding entry in
	# INDEX-OLD with type -.
	fetch_filter_unmodified_notpresent	\
	    INDEX-OLD INDEX-PRESENT INDEX-NEW /dev/null

	# For each entry in INDEX-PRESENT of type -, remove any corresponding
	# entry from INDEX-NEW if ${ALLOWADD} != "yes".  Remove all entries
	# of type - from INDEX-PRESENT.
	fetch_filter_allowadd INDEX-PRESENT INDEX-NEW

	# If ${ALLOWDELETE} != "yes", then remove any entries from
	# INDEX-PRESENT which don't correspond to entries in INDEX-NEW.
	fetch_filter_allowdelete INDEX-PRESENT INDEX-NEW

	# If ${KEEPMODIFIEDMETADATA} == "yes", then for each entry in
	# INDEX-PRESENT with metadata not matching any entry in INDEX-OLD,
	# replace the corresponding line of INDEX-NEW with one having the
	# same metadata as the entry in INDEX-PRESENT.
	fetch_filter_modified_metadata INDEX-OLD INDEX-PRESENT INDEX-NEW

	# Remove lines from INDEX-PRESENT and INDEX-NEW which are identical;
	# no need to update a file if it isn't changing.
	fetch_filter_uptodate INDEX-PRESENT INDEX-NEW

	# Prepare to fetch files: Generate a list of the files we need,
	# copy the unmodified files we have into /files/, and generate
	# a list of patches to download.
	fetch_files_prepare INDEX-OLD INDEX-PRESENT INDEX-NEW || return 1

	# Fetch files.
	fetch_files || return 1

	# Create and populate install manifest directory; and report what
	# updates are available.
	fetch_create_manifest || return 1

	# Warn about any upcoming EoL
	fetch_warn_eol || return 1
}

# If StrictComponents is not "yes", generate a new components list
# with only the components which appear to be installed.
upgrade_guess_components () {
	if [ "${STRICTCOMPONENTS}" = "no" ]; then
		# Generate filtered INDEX-ALL with only the components listed
		# in COMPONENTS.
		fetch_filter_metadata_components $1 || return 1

		# Tell the user why his disk is suddenly making lots of noise
		echo -n "Inspecting system... "

		# Look at the files on disk, and assume that a component is
		# supposed to be present if it is more than half-present.
		cut -f 1-3 -d '|' < INDEX-ALL |
		    tr '|' ' ' |
		    while read C S F; do
			if [ -e ${BASEDIR}/${F} ]; then
				echo "+ ${C}|${S}"
			fi
			echo "= ${C}|${S}"
		    done |
		    sort |
		    uniq -c |
		    sed -E 's,^ +,,' > compfreq
		grep ' = ' compfreq |
		    cut -f 1,3 -d ' ' |
		    sort -k 2,2 -t ' ' > compfreq.total
		grep ' + ' compfreq |
		    cut -f 1,3 -d ' ' |
		    sort -k 2,2 -t ' ' > compfreq.present
		join -t ' ' -1 2 -2 2 compfreq.present compfreq.total |
		    while read S P T; do
			if [ ${T} -ne 0 -a ${P} -gt `expr ${T} / 2` ]; then
				echo ${S}
			fi
		    done > comp.present
		cut -f 2 -d ' ' < compfreq.total > comp.total
		rm INDEX-ALL compfreq compfreq.total compfreq.present

		# We're done making noise.
		echo "done."

		# Sometimes the kernel isn't installed where INDEX-ALL
		# thinks that it should be: In particular, it is often in
		# /boot/kernel instead of /boot/GENERIC or /boot/SMP.  To
		# deal with this, if "kernel|X" is listed in comp.total
		# (i.e., is a component which would be upgraded if it is
		# found to be present) we will add it to comp.present.
		# If "kernel|<anything>" is in comp.total but "kernel|X" is
		# not, we print a warning -- the user is running a kernel
		# which isn't part of the release.
		KCOMP=`echo ${KERNCONF} | tr 'A-Z' 'a-z'`
		grep -E "^kernel\|${KCOMP}\$" comp.total >> comp.present

		if grep -qE "^kernel\|" comp.total &&
		    ! grep -qE "^kernel\|${KCOMP}\$" comp.total; then
			cat <<-EOF

WARNING: This system is running a "${KCOMP}" kernel, which is not a
kernel configuration distributed as part of FreeBSD ${RELNUM}.
This kernel will not be updated: you MUST update the kernel manually
before running "$0 install".
			EOF
		fi

		# Re-sort the list of installed components and generate
		# the list of non-installed components.
		sort -u < comp.present > comp.present.tmp
		mv comp.present.tmp comp.present
		comm -13 comp.present comp.total > comp.absent

		# Ask the user to confirm that what we have is correct.  To
		# reduce user confusion, translate "X|Y" back to "X/Y" (as
		# subcomponents must be listed in the configuration file).
		echo
		echo -n "The following components of FreeBSD "
		echo "seem to be installed:"
		tr '|' '/' < comp.present |
		    fmt -72
		echo
		echo -n "The following components of FreeBSD "
		echo "do not seem to be installed:"
		tr '|' '/' < comp.absent |
		    fmt -72
		echo
		continuep || return 1
		echo

		# Suck the generated list of components into ${COMPONENTS}.
		# Note that comp.present.tmp is used due to issues with
		# pipelines and setting variables.
		COMPONENTS=""
		tr '|' '/' < comp.present > comp.present.tmp
		while read C; do
			COMPONENTS="${COMPONENTS} ${C}"
		done < comp.present.tmp

		# Delete temporary files
		rm comp.present comp.present.tmp comp.absent comp.total
	fi
}

# If StrictComponents is not "yes", COMPONENTS contains an entry
# corresponding to the currently running kernel, and said kernel
# does not exist in the new release, add "kernel/generic" to the
# list of components.
upgrade_guess_new_kernel () {
	if [ "${STRICTCOMPONENTS}" = "no" ]; then
		# Grab the unfiltered metadata file.
		METAHASH=`look "$1|" tINDEX.present | cut -f 2 -d '|'`
		gunzip -c < files/${METAHASH}.gz > $1.all

		# If "kernel/${KCOMP}" is in ${COMPONENTS} and that component
		# isn't in $1.all, we need to add kernel/generic.
		for C in ${COMPONENTS}; do
			if [ ${C} = "kernel/${KCOMP}" ] &&
			    ! grep -qE "^kernel\|${KCOMP}\|" $1.all; then
				COMPONENTS="${COMPONENTS} kernel/generic"
				NKERNCONF="GENERIC"
				cat <<-EOF

WARNING: This system is running a "${KCOMP}" kernel, which is not a
kernel configuration distributed as part of FreeBSD ${RELNUM}.
As part of upgrading to FreeBSD ${RELNUM}, this kernel will be
replaced with a "generic" kernel.
				EOF
				continuep || return 1
			fi
		done

		# Don't need this any more...
		rm $1.all
	fi
}

# Convert INDEX-OLD (last release) and INDEX-ALL (new release) into
# INDEX-OLD and INDEX-NEW files (in the sense of normal upgrades).
upgrade_oldall_to_oldnew () {
	# For each ${F}|... which appears in INDEX-ALL but does not appear
	# in INDEX-OLD, add ${F}|-|||||| to INDEX-OLD.
	cut -f 1 -d '|' < $1 |
	    sort -u > $1.paths
	cut -f 1 -d '|' < $2 |
	    sort -u |
	    comm -13 $1.paths - |
	    lam - -s "|-||||||" |
	    sort - $1 > $1.tmp
	mv $1.tmp $1

	# Remove lines from INDEX-OLD which also appear in INDEX-ALL
	comm -23 $1 $2 > $1.tmp
	mv $1.tmp $1

	# Remove lines from INDEX-ALL which have a file name not appearing
	# anywhere in INDEX-OLD (since these must be files which haven't
	# changed -- if they were new, there would be an entry of type "-").
	cut -f 1 -d '|' < $1 |
	    sort -u > $1.paths
	sort -k 1,1 -t '|' < $2 |
	    join -t '|' - $1.paths |
	    sort > $2.tmp
	rm $1.paths
	mv $2.tmp $2

	# Rename INDEX-ALL to INDEX-NEW.
	mv $2 $3
}

# Helper for upgrade_merge: Return zero true iff the two files differ only
# in the contents of their RCS tags.
samef () {
	X=`sed -E 's/\\$FreeBSD.*\\$/\$FreeBSD\$/' < $1 | ${SHA256}`
	Y=`sed -E 's/\\$FreeBSD.*\\$/\$FreeBSD\$/' < $2 | ${SHA256}`

	if [ $X = $Y ]; then
		return 0;
	else
		return 1;
	fi
}

# From the list of "old" files in $1, merge changes in $2 with those in $3,
# and update $3 to reflect the hashes of merged files.
upgrade_merge () {
	# We only need to do anything if $1 is non-empty.
	if [ -s $1 ]; then
		cut -f 1 -d '|' $1 |
		    sort > $1-paths

		# Create staging area for merging files
		rm -rf merge/
		while read F; do
			D=`dirname ${F}`
			mkdir -p merge/old/${D}
			mkdir -p merge/${OLDRELNUM}/${D}
			mkdir -p merge/${RELNUM}/${D}
			mkdir -p merge/new/${D}
		done < $1-paths

		# Copy in files
		while read F; do
			# Currently installed file
			V=`look "${F}|" $2 | cut -f 7 -d '|'`
			gunzip < files/${V}.gz > merge/old/${F}

			# Old release
			if look "${F}|" $1 | fgrep -q "|f|"; then
				V=`look "${F}|" $1 | cut -f 3 -d '|'`
				gunzip < files/${V}.gz		\
				    > merge/${OLDRELNUM}/${F}
			fi

			# New release
			if look "${F}|" $3 | cut -f 1,2,7 -d '|' |
			    fgrep -q "|f|"; then
				V=`look "${F}|" $3 | cut -f 7 -d '|'`
				gunzip < files/${V}.gz		\
				    > merge/${RELNUM}/${F}
			fi
		done < $1-paths

		# Attempt to automatically merge changes
		echo -n "Attempting to automatically merge "
		echo -n "changes in files..."
		: > failed.merges
		while read F; do
			# If the file doesn't exist in the new release,
			# the result of "merging changes" is having the file
			# not exist.
			if ! [ -f merge/${RELNUM}/${F} ]; then
				continue
			fi

			# If the file didn't exist in the old release, we're
			# going to throw away the existing file and hope that
			# the version from the new release is what we want.
			if ! [ -f merge/${OLDRELNUM}/${F} ]; then
				cp merge/${RELNUM}/${F} merge/new/${F}
				continue
			fi

			# Some files need special treatment.
			case ${F} in
			/etc/spwd.db | /etc/pwd.db | /etc/login.conf.db)
				# Don't merge these -- we're rebuild them
				# after updates are installed.
				cp merge/old/${F} merge/new/${F}
				;;
			*)
				if ! diff3 -E -m -L "current version"	\
				    -L "${OLDRELNUM}" -L "${RELNUM}"	\
				    merge/old/${F}			\
				    merge/${OLDRELNUM}/${F}		\
				    merge/${RELNUM}/${F}		\
				    > merge/new/${F} 2>/dev/null; then
					echo ${F} >> failed.merges
				fi
				;;
			esac
		done < $1-paths
		echo " done."

		# Ask the user to handle any files which didn't merge.
		while read F; do
			# If the installed file differs from the version in
			# the old release only due to RCS tag expansion
			# then just use the version in the new release.
			if samef merge/old/${F} merge/${OLDRELNUM}/${F}; then
				cp merge/${RELNUM}/${F} merge/new/${F}
				continue
			fi

			cat <<-EOF

The following file could not be merged automatically: ${F}
Press Enter to edit this file in ${EDITOR} and resolve the conflicts
manually...
			EOF
			read dummy </dev/tty
			${EDITOR} `pwd`/merge/new/${F} < /dev/tty
		done < failed.merges
		rm failed.merges

		# Ask the user to confirm that he likes how the result
		# of merging files.
		while read F; do
			# Skip files which haven't changed except possibly
			# in their RCS tags.
			if [ -f merge/old/${F} ] && [ -f merge/new/${F} ] &&
			    samef merge/old/${F} merge/new/${F}; then
				continue
			fi

			# Skip files where the installed file differs from
			# the old file only due to RCS tags.
			if [ -f merge/old/${F} ] &&
			    [ -f merge/${OLDRELNUM}/${F} ] &&
			    samef merge/old/${F} merge/${OLDRELNUM}/${F}; then
				continue
			fi

			# Warn about files which are ceasing to exist.
			if ! [ -f merge/new/${F} ]; then
				cat <<-EOF

The following file will be removed, as it no longer exists in
FreeBSD ${RELNUM}: ${F}
				EOF
				continuep < /dev/tty || return 1
				continue
			fi

			# Print changes for the user's approval.
			cat <<-EOF

The following changes, which occurred between FreeBSD ${OLDRELNUM} and
FreeBSD ${RELNUM} have been merged into ${F}:
EOF
			diff -U 5 -L "current version" -L "new version"	\
			    merge/old/${F} merge/new/${F} || true
			continuep < /dev/tty || return 1
		done < $1-paths

		# Store merged files.
		while read F; do
			if [ -f merge/new/${F} ]; then
				V=`${SHA256} -q merge/new/${F}`

				gzip -c < merge/new/${F} > files/${V}.gz
				echo "${F}|${V}"
			fi
		done < $1-paths > newhashes

		# Pull lines out from $3 which need to be updated to
		# reflect merged files.
		while read F; do
			look "${F}|" $3
		done < $1-paths > $3-oldlines

		# Update lines to reflect merged files
		join -t '|' -o 1.1,1.2,1.3,1.4,1.5,1.6,2.2,1.8		\
		    $3-oldlines newhashes > $3-newlines

		# Remove old lines from $3 and add new lines.
		sort $3-oldlines |
		    comm -13 - $3 |
		    sort - $3-newlines > $3.tmp
		mv $3.tmp $3

		# Clean up
		rm $1-paths newhashes $3-oldlines $3-newlines
		rm -rf merge/
	fi

	# We're done with merging files.
	rm $1
}

# Do the work involved in fetching upgrades to a new release
upgrade_run () {
	workdir_init || return 1

	# Prepare the mirror list.
	fetch_pick_server_init && fetch_pick_server

	# Try to fetch the public key until we run out of servers.
	while ! fetch_key; do
		fetch_pick_server || return 1
	done
 
	# Try to fetch the metadata index signature ("tag") until we run
	# out of available servers; and sanity check the downloaded tag.
	while ! fetch_tag; do
		fetch_pick_server || return 1
	done
	fetch_tagsanity || return 1

	# Fetch the INDEX-OLD and INDEX-ALL.
	fetch_metadata INDEX-OLD INDEX-ALL || return 1

	# If StrictComponents is not "yes", generate a new components list
	# with only the components which appear to be installed.
	upgrade_guess_components INDEX-ALL || return 1

	# Generate filtered INDEX-OLD and INDEX-ALL files containing only
	# the components we want and without anything marked as "Ignore".
	fetch_filter_metadata INDEX-OLD || return 1
	fetch_filter_metadata INDEX-ALL || return 1

	# Merge the INDEX-OLD and INDEX-ALL files into INDEX-OLD.
	sort INDEX-OLD INDEX-ALL > INDEX-OLD.tmp
	mv INDEX-OLD.tmp INDEX-OLD
	rm INDEX-ALL

	# Adjust variables for fetching files from the new release.
	OLDRELNUM=${RELNUM}
	RELNUM=${TARGETRELEASE}
	OLDFETCHDIR=${FETCHDIR}
	FETCHDIR=${RELNUM}/${ARCH}

	# Try to fetch the NEW metadata index signature ("tag") until we run
	# out of available servers; and sanity check the downloaded tag.
	while ! fetch_tag; do
		fetch_pick_server || return 1
	done

	# Fetch the new INDEX-ALL.
	fetch_metadata INDEX-ALL || return 1

	# If StrictComponents is not "yes", COMPONENTS contains an entry
	# corresponding to the currently running kernel, and said kernel
	# does not exist in the new release, add "kernel/generic" to the
	# list of components.
	upgrade_guess_new_kernel INDEX-ALL || return 1

	# Filter INDEX-ALL to contain only the components we want and without
	# anything marked as "Ignore".
	fetch_filter_metadata INDEX-ALL || return 1

	# Convert INDEX-OLD (last release) and INDEX-ALL (new release) into
	# INDEX-OLD and INDEX-NEW files (in the sense of normal upgrades).
	upgrade_oldall_to_oldnew INDEX-OLD INDEX-ALL INDEX-NEW

	# Translate /boot/${KERNCONF} or /boot/${NKERNCONF} into ${KERNELDIR}
	fetch_filter_kernel_names INDEX-NEW ${NKERNCONF}
	fetch_filter_kernel_names INDEX-OLD ${KERNCONF}

	# For all paths appearing in INDEX-OLD or INDEX-NEW, inspect the
	# system and generate an INDEX-PRESENT file.
	fetch_inspect_system INDEX-OLD INDEX-PRESENT INDEX-NEW || return 1

	# Based on ${MERGECHANGES}, generate a file tomerge-old with the
	# paths and hashes of old versions of files to merge.
	fetch_filter_mergechanges INDEX-OLD INDEX-PRESENT tomerge-old

	# Based on ${UPDATEIFUNMODIFIED}, remove lines from INDEX-* which
	# correspond to lines in INDEX-PRESENT with hashes not appearing
	# in INDEX-OLD or INDEX-NEW.  Also remove lines where the entry in
	# INDEX-PRESENT has type - and there isn't a corresponding entry in
	# INDEX-OLD with type -.
	fetch_filter_unmodified_notpresent	\
	    INDEX-OLD INDEX-PRESENT INDEX-NEW tomerge-old

	# For each entry in INDEX-PRESENT of type -, remove any corresponding
	# entry from INDEX-NEW if ${ALLOWADD} != "yes".  Remove all entries
	# of type - from INDEX-PRESENT.
	fetch_filter_allowadd INDEX-PRESENT INDEX-NEW

	# If ${ALLOWDELETE} != "yes", then remove any entries from
	# INDEX-PRESENT which don't correspond to entries in INDEX-NEW.
	fetch_filter_allowdelete INDEX-PRESENT INDEX-NEW

	# If ${KEEPMODIFIEDMETADATA} == "yes", then for each entry in
	# INDEX-PRESENT with metadata not matching any entry in INDEX-OLD,
	# replace the corresponding line of INDEX-NEW with one having the
	# same metadata as the entry in INDEX-PRESENT.
	fetch_filter_modified_metadata INDEX-OLD INDEX-PRESENT INDEX-NEW

	# Remove lines from INDEX-PRESENT and INDEX-NEW which are identical;
	# no need to update a file if it isn't changing.
	fetch_filter_uptodate INDEX-PRESENT INDEX-NEW

	# Fetch "clean" files from the old release for merging changes.
	fetch_files_premerge tomerge-old

	# Prepare to fetch files: Generate a list of the files we need,
	# copy the unmodified files we have into /files/, and generate
	# a list of patches to download.
	fetch_files_prepare INDEX-OLD INDEX-PRESENT INDEX-NEW || return 1

	# Fetch patches from to-${RELNUM}/${ARCH}/bp/
	PATCHDIR=to-${RELNUM}/${ARCH}/bp
	fetch_files || return 1

	# Merge configuration file changes.
	upgrade_merge tomerge-old INDEX-PRESENT INDEX-NEW || return 1

	# Create and populate install manifest directory; and report what
	# updates are available.
	fetch_create_manifest || return 1

	# Leave a note behind to tell the "install" command that the kernel
	# needs to be installed before the world.
	touch ${BDHASH}-install/kernelfirst

	# Remind the user that they need to run "freebsd-update install"
	# to install the downloaded bits, in case they didn't RTFM.
	echo "To install the downloaded upgrades, run \"$0 install\"."
}

# Make sure that all the file hashes mentioned in $@ have corresponding
# gzipped files stored in /files/.
install_verify () {
	# Generate a list of hashes
	cat $@ |
	    cut -f 2,7 -d '|' |
	    grep -E '^f' |
	    cut -f 2 -d '|' |
	    sort -u > filelist

	# Make sure all the hashes exist
	while read HASH; do
		if ! [ -f files/${HASH}.gz ]; then
			echo -n "Update files missing -- "
			echo "this should never happen."
			echo "Re-run '$0 fetch'."
			return 1
		fi
	done < filelist

	# Clean up
	rm filelist
}

# Remove the system immutable flag from files
install_unschg () {
	# Generate file list
	cat $@ |
	    cut -f 1 -d '|' > filelist

	# Remove flags
	while read F; do
		if ! [ -e ${BASEDIR}/${F} ]; then
			continue
		else
			echo ${BASEDIR}/${F}
		fi
	done < filelist | xargs chflags noschg || return 1

	# Clean up
	rm filelist
}

# Decide which directory name to use for kernel backups.
backup_kernel_finddir () {
	CNT=0
	while true ; do
		# Pathname does not exist, so it is OK use that name
		# for backup directory.
		if [ ! -e $BASEDIR/$BACKUPKERNELDIR ]; then
			return 0
		fi

		# If directory do exist, we only use if it has our
		# marker file.
		if [ -d $BASEDIR/$BACKUPKERNELDIR -a \
			-e $BASEDIR/$BACKUPKERNELDIR/.freebsd-update ]; then
			return 0
		fi

		# We could not use current directory name, so add counter to
		# the end and try again.
		CNT=$((CNT + 1))
		if [ $CNT -gt 9 ]; then
			echo "Could not find valid backup dir ($BASEDIR/$BACKUPKERNELDIR)"
			exit 1
		fi
		BACKUPKERNELDIR="`echo $BACKUPKERNELDIR | sed -Ee 's/[0-9]\$//'`"
		BACKUPKERNELDIR="${BACKUPKERNELDIR}${CNT}"
	done
}

# Backup the current kernel using hardlinks, if not disabled by user.
# Since we delete all files in the directory used for previous backups
# we create a marker file called ".freebsd-update" in the directory so
# we can determine on the next run that the directory was created by
# freebsd-update and we then do not accidentally remove user files in
# the unlikely case that the user has created a directory with a
# conflicting name.
backup_kernel () {
	# Only make kernel backup is so configured.
	if [ $BACKUPKERNEL != yes ]; then
		return 0
	fi

	# Decide which directory name to use for kernel backups.
	backup_kernel_finddir

	# Remove old kernel backup files.  If $BACKUPKERNELDIR was
	# "not ours", backup_kernel_finddir would have exited, so
	# deleting the directory content is as safe as we can make it.
	if [ -d $BASEDIR/$BACKUPKERNELDIR ]; then
		rm -fr $BASEDIR/$BACKUPKERNELDIR
	fi

	# Create directories for backup.
	mkdir -p $BASEDIR/$BACKUPKERNELDIR
	mtree -cdn -p "${BASEDIR}/${KERNELDIR}" | \
	    mtree -Ue -p "${BASEDIR}/${BACKUPKERNELDIR}" > /dev/null

	# Mark the directory as having been created by freebsd-update.
	touch $BASEDIR/$BACKUPKERNELDIR/.freebsd-update
	if [ $? -ne 0 ]; then
		echo "Could not create kernel backup directory"
		exit 1
	fi

	# Disable pathname expansion to be sure *.symbols is not
	# expanded.
	set -f

	# Use find to ignore symbol files, unless disabled by user.
	if [ $BACKUPKERNELSYMBOLFILES = yes ]; then
		FINDFILTER=""
	else
		FINDFILTER="-a ! -name *.debug -a ! -name *.symbols"
	fi

	# Backup all the kernel files using hardlinks.
	(cd ${BASEDIR}/${KERNELDIR} && find . -type f $FINDFILTER -exec \
	    cp -pl '{}' ${BASEDIR}/${BACKUPKERNELDIR}/'{}' \;)

	# Re-enable patchname expansion.
	set +f
}

# Install new files
install_from_index () {
	# First pass: Do everything apart from setting file flags.  We
	# can't set flags yet, because schg inhibits hard linking.
	sort -k 1,1 -t '|' $1 |
	    tr '|' ' ' |
	    while read FPATH TYPE OWNER GROUP PERM FLAGS HASH LINK; do
		case ${TYPE} in
		d)
			# Create a directory
			install -d -o ${OWNER} -g ${GROUP}		\
			    -m ${PERM} ${BASEDIR}/${FPATH}
			;;
		f)
			if [ -z "${LINK}" ]; then
				# Create a file, without setting flags.
				gunzip < files/${HASH}.gz > ${HASH}
				install -S -o ${OWNER} -g ${GROUP}	\
				    -m ${PERM} ${HASH} ${BASEDIR}/${FPATH}
				rm ${HASH}
			else
				# Create a hard link.
				ln -f ${BASEDIR}/${LINK} ${BASEDIR}/${FPATH}
			fi
			;;
		L)
			# Create a symlink
			ln -sfh ${HASH} ${BASEDIR}/${FPATH}
			;;
		esac
	    done

	# Perform a second pass, adding file flags.
	tr '|' ' ' < $1 |
	    while read FPATH TYPE OWNER GROUP PERM FLAGS HASH LINK; do
		if [ ${TYPE} = "f" ] &&
		    ! [ ${FLAGS} = "0" ]; then
			chflags ${FLAGS} ${BASEDIR}/${FPATH}
		fi
	    done
}

# Remove files which we want to delete
install_delete () {
	# Generate list of new files
	cut -f 1 -d '|' < $2 |
	    sort > newfiles

	# Generate subindex of old files we want to nuke
	sort -k 1,1 -t '|' $1 |
	    join -t '|' -v 1 - newfiles |
	    sort -r -k 1,1 -t '|' |
	    cut -f 1,2 -d '|' |
	    tr '|' ' ' > killfiles

	# Remove the offending bits
	while read FPATH TYPE; do
		case ${TYPE} in
		d)
			rmdir ${BASEDIR}/${FPATH}
			;;
		f)
			rm ${BASEDIR}/${FPATH}
			;;
		L)
			rm ${BASEDIR}/${FPATH}
			;;
		esac
	done < killfiles

	# Clean up
	rm newfiles killfiles
}

# Install new files, delete old files, and update linker.hints
install_files () {
	# If we haven't already dealt with the kernel, deal with it.
	if ! [ -f $1/kerneldone ]; then
		grep -E '^/boot/' $1/INDEX-OLD > INDEX-OLD
		grep -E '^/boot/' $1/INDEX-NEW > INDEX-NEW

		# Backup current kernel before installing a new one
		backup_kernel || return 1

		# Install new files
		install_from_index INDEX-NEW || return 1

		# Remove files which need to be deleted
		install_delete INDEX-OLD INDEX-NEW || return 1

		# Update linker.hints if necessary
		if [ -s INDEX-OLD -o -s INDEX-NEW ]; then
			kldxref -R ${BASEDIR}/boot/ 2>/dev/null
		fi

		# We've finished updating the kernel.
		touch $1/kerneldone

		# Do we need to ask for a reboot now?
		if [ -f $1/kernelfirst ] &&
		    [ -s INDEX-OLD -o -s INDEX-NEW ]; then
			cat <<-EOF

Kernel updates have been installed.  Please reboot and run
"$0 install" again to finish installing updates.
			EOF
			exit 0
		fi
	fi

	# If we haven't already dealt with the world, deal with it.
	if ! [ -f $1/worlddone ]; then
		# Create any necessary directories first
		grep -vE '^/boot/' $1/INDEX-NEW |
		    grep -E '^[^|]+\|d\|' > INDEX-NEW
		install_from_index INDEX-NEW || return 1

		# Install new runtime linker
		grep -vE '^/boot/' $1/INDEX-NEW |
		    grep -vE '^[^|]+\|d\|' |
		    grep -E '^/libexec/ld-elf[^|]*\.so\.[0-9]+\|' > INDEX-NEW
		install_from_index INDEX-NEW || return 1

		# Install new shared libraries next
		grep -vE '^/boot/' $1/INDEX-NEW |
		    grep -vE '^[^|]+\|d\|' |
		    grep -vE '^/libexec/ld-elf[^|]*\.so\.[0-9]+\|' |
		    grep -E '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' > INDEX-NEW
		install_from_index INDEX-NEW || return 1

		# Deal with everything else
		grep -vE '^/boot/' $1/INDEX-OLD |
		    grep -vE '^[^|]+\|d\|' |
		    grep -vE '^/libexec/ld-elf[^|]*\.so\.[0-9]+\|' |
		    grep -vE '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' > INDEX-OLD
		grep -vE '^/boot/' $1/INDEX-NEW |
		    grep -vE '^[^|]+\|d\|' |
		    grep -vE '^/libexec/ld-elf[^|]*\.so\.[0-9]+\|' |
		    grep -vE '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' > INDEX-NEW
		install_from_index INDEX-NEW || return 1
		install_delete INDEX-OLD INDEX-NEW || return 1

		# Rebuild generated pwd files.
		if [ ${BASEDIR}/etc/master.passwd -nt ${BASEDIR}/etc/spwd.db ] ||
		    [ ${BASEDIR}/etc/master.passwd -nt ${BASEDIR}/etc/pwd.db ] ||
		    [ ${BASEDIR}/etc/master.passwd -nt ${BASEDIR}/etc/passwd ]; then
			pwd_mkdb -d ${BASEDIR}/etc -p ${BASEDIR}/etc/master.passwd
		fi

		# Rebuild /etc/login.conf.db if necessary.
		if [ ${BASEDIR}/etc/login.conf -nt ${BASEDIR}/etc/login.conf.db ]; then
			cap_mkdb ${BASEDIR}/etc/login.conf
		fi

		# Rebuild man page databases, if necessary.
		for D in /usr/share/man /usr/share/openssl/man; do
			if [ ! -d ${BASEDIR}/$D ]; then
				continue
			fi
			if [ -z "$(find ${BASEDIR}/$D -type f -newer ${BASEDIR}/$D/mandoc.db)" ]; then
				continue;
			fi
			makewhatis ${BASEDIR}/$D
		done

		# We've finished installing the world and deleting old files
		# which are not shared libraries.
		touch $1/worlddone

		# Do we need to ask the user to portupgrade now?
		grep -vE '^/boot/' $1/INDEX-NEW |
		    grep -E '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' |
		    cut -f 1 -d '|' |
		    sort > newfiles
		if grep -vE '^/boot/' $1/INDEX-OLD |
		    grep -E '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' |
		    cut -f 1 -d '|' |
		    sort |
		    join -v 1 - newfiles |
		    grep -q .; then
			cat <<-EOF

Completing this upgrade requires removing old shared object files.
Please rebuild all installed 3rd party software (e.g., programs
installed from the ports tree) and then run "$0 install"
again to finish installing updates.
			EOF
			rm newfiles
			exit 0
		fi
		rm newfiles
	fi

	# Remove old shared libraries
	grep -vE '^/boot/' $1/INDEX-NEW |
	    grep -vE '^[^|]+\|d\|' |
	    grep -E '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' > INDEX-NEW
	grep -vE '^/boot/' $1/INDEX-OLD |
	    grep -vE '^[^|]+\|d\|' |
	    grep -E '^[^|]*/lib/[^|]*\.so\.[0-9]+\|' > INDEX-OLD
	install_delete INDEX-OLD INDEX-NEW || return 1

	# Remove old directories
	grep -vE '^/boot/' $1/INDEX-NEW |
	    grep -E '^[^|]+\|d\|' > INDEX-NEW
	grep -vE '^/boot/' $1/INDEX-OLD |
	    grep -E '^[^|]+\|d\|' > INDEX-OLD
	install_delete INDEX-OLD INDEX-NEW || return 1

	# Remove temporary files
	rm INDEX-OLD INDEX-NEW
}

# Rearrange bits to allow the installed updates to be rolled back
install_setup_rollback () {
	# Remove the "reboot after installing kernel", "kernel updated", and
	# "finished installing the world" flags if present -- they are
	# irrelevant when rolling back updates.
	if [ -f ${BDHASH}-install/kernelfirst ]; then
		rm ${BDHASH}-install/kernelfirst
		rm ${BDHASH}-install/kerneldone
	fi
	if [ -f ${BDHASH}-install/worlddone ]; then
		rm ${BDHASH}-install/worlddone
	fi

	if [ -L ${BDHASH}-rollback ]; then
		mv ${BDHASH}-rollback ${BDHASH}-install/rollback
	fi

	mv ${BDHASH}-install ${BDHASH}-rollback
}

# Actually install updates
install_run () {
	echo -n "Installing updates..."

	# Make sure we have all the files we should have
	install_verify ${BDHASH}-install/INDEX-OLD	\
	    ${BDHASH}-install/INDEX-NEW || return 1

	# Remove system immutable flag from files
	install_unschg ${BDHASH}-install/INDEX-OLD	\
	    ${BDHASH}-install/INDEX-NEW || return 1

	# Install new files, delete old files, and update linker.hints
	install_files ${BDHASH}-install || return 1

	# Rearrange bits to allow the installed updates to be rolled back
	install_setup_rollback

	echo " done."
}

# Rearrange bits to allow the previous set of updates to be rolled back next.
rollback_setup_rollback () {
	if [ -L ${BDHASH}-rollback/rollback ]; then
		mv ${BDHASH}-rollback/rollback rollback-tmp
		rm -r ${BDHASH}-rollback/
		rm ${BDHASH}-rollback
		mv rollback-tmp ${BDHASH}-rollback
	else
		rm -r ${BDHASH}-rollback/
		rm ${BDHASH}-rollback
	fi
}

# Install old files, delete new files, and update linker.hints
rollback_files () {
	# Install old shared library files which don't have the same path as
	# a new shared library file.
	grep -vE '^/boot/' $1/INDEX-NEW |
	    grep -E '/lib/.*\.so\.[0-9]+\|' |
	    cut -f 1 -d '|' |
	    sort > INDEX-NEW.libs.flist
	grep -vE '^/boot/' $1/INDEX-OLD |
	    grep -E '/lib/.*\.so\.[0-9]+\|' |
	    sort -k 1,1 -t '|' - |
	    join -t '|' -v 1 - INDEX-NEW.libs.flist > INDEX-OLD
	install_from_index INDEX-OLD || return 1

	# Deal with files which are neither kernel nor shared library
	grep -vE '^/boot/' $1/INDEX-OLD |
	    grep -vE '/lib/.*\.so\.[0-9]+\|' > INDEX-OLD
	grep -vE '^/boot/' $1/INDEX-NEW |
	    grep -vE '/lib/.*\.so\.[0-9]+\|' > INDEX-NEW
	install_from_index INDEX-OLD || return 1
	install_delete INDEX-NEW INDEX-OLD || return 1

	# Install any old shared library files which we didn't install above.
	grep -vE '^/boot/' $1/INDEX-OLD |
	    grep -E '/lib/.*\.so\.[0-9]+\|' |
	    sort -k 1,1 -t '|' - |
	    join -t '|' - INDEX-NEW.libs.flist > INDEX-OLD
	install_from_index INDEX-OLD || return 1

	# Delete unneeded shared library files
	grep -vE '^/boot/' $1/INDEX-OLD |
	    grep -E '/lib/.*\.so\.[0-9]+\|' > INDEX-OLD
	grep -vE '^/boot/' $1/INDEX-NEW |
	    grep -E '/lib/.*\.so\.[0-9]+\|' > INDEX-NEW
	install_delete INDEX-NEW INDEX-OLD || return 1

	# Deal with kernel files
	grep -E '^/boot/' $1/INDEX-OLD > INDEX-OLD
	grep -E '^/boot/' $1/INDEX-NEW > INDEX-NEW
	install_from_index INDEX-OLD || return 1
	install_delete INDEX-NEW INDEX-OLD || return 1
	if [ -s INDEX-OLD -o -s INDEX-NEW ]; then
		kldxref -R /boot/ 2>/dev/null
	fi

	# Remove temporary files
	rm INDEX-OLD INDEX-NEW INDEX-NEW.libs.flist
}

# Actually rollback updates
rollback_run () {
	echo -n "Uninstalling updates..."

	# If there are updates waiting to be installed, remove them; we
	# want the user to re-run 'fetch' after rolling back updates.
	if [ -L ${BDHASH}-install ]; then
		rm -r ${BDHASH}-install/
		rm ${BDHASH}-install
	fi

	# Make sure we have all the files we should have
	install_verify ${BDHASH}-rollback/INDEX-NEW	\
	    ${BDHASH}-rollback/INDEX-OLD || return 1

	# Remove system immutable flag from files
	install_unschg ${BDHASH}-rollback/INDEX-NEW	\
	    ${BDHASH}-rollback/INDEX-OLD || return 1

	# Install old files, delete new files, and update linker.hints
	rollback_files ${BDHASH}-rollback || return 1

	# Remove the rollback directory and the symlink pointing to it; and
	# rearrange bits to allow the previous set of updates to be rolled
	# back next.
	rollback_setup_rollback

	echo " done."
}

# Compare INDEX-ALL and INDEX-PRESENT and print warnings about differences.
IDS_compare () {
	# Get all the lines which mismatch in something other than file
	# flags.  We ignore file flags because sysinstall doesn't seem to
	# set them when it installs FreeBSD; warning about these adds a
	# very large amount of noise.
	cut -f 1-5,7-8 -d '|' $1 > $1.noflags
	sort -k 1,1 -t '|' $1.noflags > $1.sorted
	cut -f 1-5,7-8 -d '|' $2 |
	    comm -13 $1.noflags - |
	    fgrep -v '|-|||||' |
	    sort -k 1,1 -t '|' |
	    join -t '|' $1.sorted - > INDEX-NOTMATCHING

	# Ignore files which match IDSIGNOREPATHS.
	for X in ${IDSIGNOREPATHS}; do
		grep -E "^${X}" INDEX-NOTMATCHING
	done |
	    sort -u |
	    comm -13 - INDEX-NOTMATCHING > INDEX-NOTMATCHING.tmp
	mv INDEX-NOTMATCHING.tmp INDEX-NOTMATCHING

	# Go through the lines and print warnings.
	local IFS='|'
	while read FPATH TYPE OWNER GROUP PERM HASH LINK P_TYPE P_OWNER P_GROUP P_PERM P_HASH P_LINK; do
		# Warn about different object types.
		if ! [ "${TYPE}" = "${P_TYPE}" ]; then
			echo -n "${FPATH} is a "
			case "${P_TYPE}" in
			f)	echo -n "regular file, "
				;;
			d)	echo -n "directory, "
				;;
			L)	echo -n "symlink, "
				;;
			esac
			echo -n "but should be a "
			case "${TYPE}" in
			f)	echo -n "regular file."
				;;
			d)	echo -n "directory."
				;;
			L)	echo -n "symlink."
				;;
			esac
			echo

			# Skip other tests, since they don't make sense if
			# we're comparing different object types.
			continue
		fi

		# Warn about different owners.
		if ! [ "${OWNER}" = "${P_OWNER}" ]; then
			echo -n "${FPATH} is owned by user id ${P_OWNER}, "
			echo "but should be owned by user id ${OWNER}."
		fi

		# Warn about different groups.
		if ! [ "${GROUP}" = "${P_GROUP}" ]; then
			echo -n "${FPATH} is owned by group id ${P_GROUP}, "
			echo "but should be owned by group id ${GROUP}."
		fi

		# Warn about different permissions.  We do not warn about
		# different permissions on symlinks, since some archivers
		# don't extract symlink permissions correctly and they are
		# ignored anyway.
		if ! [ "${PERM}" = "${P_PERM}" ] &&
		    ! [ "${TYPE}" = "L" ]; then
			echo -n "${FPATH} has ${P_PERM} permissions, "
			echo "but should have ${PERM} permissions."
		fi

		# Warn about different file hashes / symlink destinations.
		if ! [ "${HASH}" = "${P_HASH}" ]; then
			if [ "${TYPE}" = "L" ]; then
				echo -n "${FPATH} is a symlink to ${P_HASH}, "
				echo "but should be a symlink to ${HASH}."
			fi
			if [ "${TYPE}" = "f" ]; then
				echo -n "${FPATH} has SHA256 hash ${P_HASH}, "
				echo "but should have SHA256 hash ${HASH}."
			fi
		fi

		# We don't warn about different hard links, since some
		# some archivers break hard links, and as long as the
		# underlying data is correct they really don't matter.
	done < INDEX-NOTMATCHING

	# Clean up
	rm $1 $1.noflags $1.sorted $2 INDEX-NOTMATCHING
}

# Do the work involved in comparing the system to a "known good" index
IDS_run () {
	workdir_init || return 1

	# Prepare the mirror list.
	fetch_pick_server_init && fetch_pick_server

	# Try to fetch the public key until we run out of servers.
	while ! fetch_key; do
		fetch_pick_server || return 1
	done
 
	# Try to fetch the metadata index signature ("tag") until we run
	# out of available servers; and sanity check the downloaded tag.
	while ! fetch_tag; do
		fetch_pick_server || return 1
	done
	fetch_tagsanity || return 1

	# Fetch INDEX-OLD and INDEX-ALL.
	fetch_metadata INDEX-OLD INDEX-ALL || return 1

	# Generate filtered INDEX-OLD and INDEX-ALL files containing only
	# the components we want and without anything marked as "Ignore".
	fetch_filter_metadata INDEX-OLD || return 1
	fetch_filter_metadata INDEX-ALL || return 1

	# Merge the INDEX-OLD and INDEX-ALL files into INDEX-ALL.
	sort INDEX-OLD INDEX-ALL > INDEX-ALL.tmp
	mv INDEX-ALL.tmp INDEX-ALL
	rm INDEX-OLD

	# Translate /boot/${KERNCONF} to ${KERNELDIR}
	fetch_filter_kernel_names INDEX-ALL ${KERNCONF}

	# Inspect the system and generate an INDEX-PRESENT file.
	fetch_inspect_system INDEX-ALL INDEX-PRESENT /dev/null || return 1

	# Compare INDEX-ALL and INDEX-PRESENT and print warnings about any
	# differences.
	IDS_compare INDEX-ALL INDEX-PRESENT
}

#### Main functions -- call parameter-handling and core functions

# Using the command line, configuration file, and defaults,
# set all the parameters which are needed later.
get_params () {
	init_params
	parse_cmdline $@
	parse_conffile
	default_params
}

# Fetch command.  Make sure that we're being called
# interactively, then run fetch_check_params and fetch_run
cmd_fetch () {
	if [ ! -t 0 -a $NOTTYOK -eq 0 ]; then
		echo -n "`basename $0` fetch should not "
		echo "be run non-interactively."
		echo "Run `basename $0` cron instead."
		exit 1
	fi
	fetch_check_params
	fetch_run || exit 1
	ISFETCHED=1
}

# Cron command.  Make sure the parameters are sensible; wait
# rand(3600) seconds; then fetch updates.  While fetching updates,
# send output to a temporary file; only print that file if the
# fetching failed.
cmd_cron () {
	fetch_check_params
	sleep `jot -r 1 0 3600`

	TMPFILE=`mktemp /tmp/freebsd-update.XXXXXX` || exit 1
	if ! fetch_run >> ${TMPFILE} ||
	    ! grep -q "No updates needed" ${TMPFILE} ||
	    [ ${VERBOSELEVEL} = "debug" ]; then
		mail -s "`hostname` security updates" ${MAILTO} < ${TMPFILE}
	fi

	rm ${TMPFILE}
}

# Fetch files for upgrading to a new release.
cmd_upgrade () {
	upgrade_check_params
	upgrade_run || exit 1
}

# Install downloaded updates.
cmd_install () {
	install_check_params
	install_run || exit 1
}

# Rollback most recently installed updates.
cmd_rollback () {
	rollback_check_params
	rollback_run || exit 1
}

# Compare system against a "known good" index.
cmd_IDS () {
	IDS_check_params
	IDS_run || exit 1
}

#### Entry point

# Make sure we find utilities from the base system
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:${PATH}

# Set a pager if the user doesn't
if [ -z "$PAGER" ]; then
	PAGER=/usr/bin/less
fi

# Set LC_ALL in order to avoid problems with character ranges like [A-Z].
export LC_ALL=C

get_params $@
for COMMAND in ${COMMANDS}; do
	cmd_${COMMAND}
done
