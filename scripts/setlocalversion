#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# This scripts adds local version information from the version
# control system git.
#
# If something goes wrong, send a mail the kernel build mailinglist
# (see MAINTAINERS) and CC Nico Schottelius
# <nico-linuxsetlocalversion -at- schottelius.org>.
#
#

usage() {
	echo "Usage: $0 [--no-local] [srctree]" >&2
	exit 1
}

no_local=false
if test "$1" = "--no-local"; then
	no_local=true
	shift
fi

srctree=.
if test $# -gt 0; then
	srctree=$1
	shift
fi
if test $# -gt 0 -o ! -d "$srctree"; then
	usage
fi

scm_version()
{
	local short=false
	local no_dirty=false
	local tag

	while [ $# -gt 0 ];
	do
		case "$1" in
		--short)
			short=true;;
		--no-dirty)
			no_dirty=true;;
		esac
		shift
	done

	cd "$srctree"

	if test -n "$(git rev-parse --show-cdup 2>/dev/null)"; then
		return
	fi

	if ! head=$(git rev-parse --verify HEAD 2>/dev/null); then
		return
	fi

	# mainline kernel:  6.2.0-rc5  ->  v6.2-rc5
	# stable kernel:    6.1.7      ->  v6.1.7
	version_tag=v$(echo "${KERNELVERSION}" | sed -E 's/^([0-9]+\.[0-9]+)\.0(.*)$/\1\2/')

	# If a localversion* file exists, and the corresponding
	# annotated tag exists and is an ancestor of HEAD, use
	# it. This is the case in linux-next.
	tag=${file_localversion#-}
	desc=
	if [ -n "${tag}" ]; then
		desc=$(git describe --match=$tag 2>/dev/null)
	fi

	# Otherwise, if a localversion* file exists, and the tag
	# obtained by appending it to the tag derived from
	# KERNELVERSION exists and is an ancestor of HEAD, use
	# it. This is e.g. the case in linux-rt.
	if [ -z "${desc}" ] && [ -n "${file_localversion}" ]; then
		tag="${version_tag}${file_localversion}"
		desc=$(git describe --match=$tag 2>/dev/null)
	fi

	# Otherwise, default to the annotated tag derived from KERNELVERSION.
	if [ -z "${desc}" ]; then
		tag="${version_tag}"
		desc=$(git describe --match=$tag 2>/dev/null)
	fi

	# If we are at the tagged commit, we ignore it because the version is
	# well-defined.
	if [ "${tag}" != "${desc}" ]; then

		# If only the short version is requested, don't bother
		# running further git commands
		if $short; then
			echo "+"
			return
		fi
		# If we are past the tagged commit, we pretty print it.
		# (like 6.1.0-14595-g292a089d78d3)
		if [ -n "${desc}" ]; then
			echo "${desc}" | awk -F- '{printf("-%05d", $(NF-1))}'
		fi

		# Add -g and exactly 12 hex chars.
		printf '%s%s' -g "$(echo $head | cut -c1-12)"
	fi

	if ${no_dirty}; then
		return
	fi

	# Check for uncommitted changes.
	# This script must avoid any write attempt to the source tree, which
	# might be read-only.
	# You cannot use 'git describe --dirty' because it tries to create
	# .git/index.lock .
	# First, with git-status, but --no-optional-locks is only supported in
	# git >= 2.14, so fall back to git-diff-index if it fails. Note that
	# git-diff-index does not refresh the index, so it may give misleading
	# results.
	# See git-update-index(1), git-diff-index(1), and git-status(1).
	if {
		git --no-optional-locks status -uno --porcelain 2>/dev/null ||
		git diff-index --name-only HEAD
	} | read dummy; then
		printf '%s' -dirty
	fi
}

collect_files()
{
	local file res=

	for file; do
		case "$file" in
		*\~*)
			continue
			;;
		esac
		if test -e "$file"; then
			res="$res$(cat "$file")"
		fi
	done
	echo "$res"
}

if [ -z "${KERNELVERSION}" ]; then
	echo "KERNELVERSION is not set" >&2
	exit 1
fi

# localversion* files in the build and source directory
file_localversion="$(collect_files localversion*)"
if test ! "$srctree" -ef .; then
	file_localversion="${file_localversion}$(collect_files "$srctree"/localversion*)"
fi

if ${no_local}; then
	echo "${KERNELVERSION}$(scm_version --no-dirty)"
	exit 0
fi

if ! test -e include/config/auto.conf; then
	echo "Error: kernelrelease not valid - run 'make prepare' to update it" >&2
	exit 1
fi

# version string from CONFIG_LOCALVERSION
config_localversion=$(sed -n 's/^CONFIG_LOCALVERSION=\(.*\)$/\1/p' include/config/auto.conf)

# scm version string if not at the kernel version tag or at the file_localversion
if grep -q "^CONFIG_LOCALVERSION_AUTO=y$" include/config/auto.conf; then
	# full scm version string
	scm_version="$(scm_version)"
elif [ "${LOCALVERSION+set}" != "set" ]; then
	# If the variable LOCALVERSION is not set, append a plus
	# sign if the repository is not in a clean annotated or
	# signed tagged state (as git describe only looks at signed
	# or annotated tags - git tag -a/-s).
	#
	# If the variable LOCALVERSION is set (including being set
	# to an empty string), we don't want to append a plus sign.
	scm_version="$(scm_version --short)"
fi

echo "${KERNELVERSION}${file_localversion}${config_localversion}${LOCALVERSION}${scm_version}"
