#!/bin/sh
# Copyright (C) Martin Schlemmer <azarah@nosferatu.za.org>
# Copyright (C) 2006 Sam Ravnborg <sam@ravnborg.org>
#
# Released under the terms of the GNU GPL
#
# Generate a cpio packed initramfs. It uses gen_init_cpio to generate
# the cpio archive.
# This script assumes that gen_init_cpio is located in usr/ directory

# error out on errors
set -e

usage() {
cat << EOF
Usage:
$0 [-o <file>] [-l <dep_list>] [-u <uid>] [-g <gid>] {-d | <cpio_source>} ...
	-o <file>      Create initramfs file named <file> by using gen_init_cpio
	-l <dep_list>  Create dependency list named <dep_list>
	-u <uid>       User ID to map to user ID 0 (root).
		       <uid> is only meaningful if <cpio_source> is a
		       directory.  "squash" forces all files to uid 0.
	-g <gid>       Group ID to map to group ID 0 (root).
		       <gid> is only meaningful if <cpio_source> is a
		       directory.  "squash" forces all files to gid 0.
	<cpio_source>  File list or directory for cpio archive.
		       If <cpio_source> is a .cpio file it will be used
		       as direct input to initramfs.

All options except -o and -l may be repeated and are interpreted
sequentially and immediately.  -u and -g states are preserved across
<cpio_source> options so an explicit "-u 0 -g 0" is required
to reset the root/group mapping.
EOF
}

# awk style field access
# $1 - field number; rest is argument string
field() {
	shift $1 ; echo $1
}

filetype() {
	local argv1="$1"

	# symlink test must come before file test
	if [ -L "${argv1}" ]; then
		echo "slink"
	elif [ -f "${argv1}" ]; then
		echo "file"
	elif [ -d "${argv1}" ]; then
		echo "dir"
	elif [ -b "${argv1}" -o -c "${argv1}" ]; then
		echo "nod"
	elif [ -p "${argv1}" ]; then
		echo "pipe"
	elif [ -S "${argv1}" ]; then
		echo "sock"
	else
		echo "invalid"
	fi
	return 0
}

print_mtime() {
	local my_mtime="0"

	if [ -e "$1" ]; then
		my_mtime=$(find "$1" -printf "%T@\n" | sort -r | head -n 1)
	fi

	echo "# Last modified: ${my_mtime}" >> $cpio_list
	echo "" >> $cpio_list
}

list_parse() {
	if [ -z "$dep_list" -o -L "$1" ]; then
		return
	fi
	echo "$1" | sed 's/:/\\:/g; s/$/ \\/' >> $dep_list
}

# for each file print a line in following format
# <filetype> <name> <path to file> <octal mode> <uid> <gid>
# for links, devices etc the format differs. See gen_init_cpio for details
parse() {
	local location="$1"
	local name="/${location#${srcdir}}"
	# change '//' into '/'
	name=$(echo "$name" | sed -e 's://*:/:g')
	local mode="$2"
	local uid="$3"
	local gid="$4"
	local ftype=$(filetype "${location}")
	# remap uid/gid to 0 if necessary
	[ "$root_uid" = "squash" ] && uid=0 || [ "$uid" -eq "$root_uid" ] && uid=0
	[ "$root_gid" = "squash" ] && gid=0 || [ "$gid" -eq "$root_gid" ] && gid=0
	local str="${mode} ${uid} ${gid}"

	[ "${ftype}" = "invalid" ] && return 0
	[ "${location}" = "${srcdir}" ] && return 0

	case "${ftype}" in
		"file")
			str="${ftype} ${name} ${location} ${str}"
			;;
		"nod")
			local dev="`LC_ALL=C ls -l "${location}"`"
			local maj=`field 5 ${dev}`
			local min=`field 6 ${dev}`
			maj=${maj%,}

			[ -b "${location}" ] && dev="b" || dev="c"

			str="${ftype} ${name} ${str} ${dev} ${maj} ${min}"
			;;
		"slink")
			local target=`readlink "${location}"`
			str="${ftype} ${name} ${target} ${str}"
			;;
		*)
			str="${ftype} ${name} ${str}"
			;;
	esac

	echo "${str}" >> $cpio_list

	return 0
}

unknown_option() {
	printf "ERROR: unknown option \"$arg\"\n" >&2
	printf "If the filename validly begins with '-', " >&2
	printf "then it must be prefixed\n" >&2
	printf "by './' so that it won't be interpreted as an option." >&2
	printf "\n" >&2
	usage >&2
	exit 1
}

header() {
	printf "\n#####################\n# $1\n" >> $cpio_list
}

# process one directory (incl sub-directories)
dir_filelist() {
	header "$1"

	srcdir=$(echo "$1" | sed -e 's://*:/:g')
	dirlist=$(find "${srcdir}" -printf "%p %m %U %G\n" | LC_ALL=C sort)

	# If $dirlist is only one line, then the directory is empty
	if [  "$(echo "${dirlist}" | wc -l)" -gt 1 ]; then
		print_mtime "$1"

		echo "${dirlist}" | \
		while read x; do
			list_parse $x
			parse $x
		done
	fi
}

input_file() {
	source="$1"
	if [ -f "$1" ]; then
		# If a regular file is specified, assume it is in
		# gen_init_cpio format
		header "$1"
		print_mtime "$1" >> $cpio_list
		cat "$1"         >> $cpio_list
		if [ -n "$dep_list" ]; then
		        echo "$1 \\"  >> $dep_list
			cat "$1" | while read type dir file perm ; do
				if [ "$type" = "file" ]; then
					echo "$file \\" >> $dep_list
				fi
			done
		fi
	elif [ -d "$1" ]; then
		# If a directory is specified then add all files in it to fs
		dir_filelist "$1"
	else
		echo "  ${prog}: Cannot open '$1'" >&2
		exit 1
	fi
}

prog=$0
root_uid=0
root_gid=0
dep_list=
cpio_list=$(mktemp ${TMPDIR:-/tmp}/cpiolist.XXXXXX)
output="/dev/stdout"

trap "rm -f $cpio_list" EXIT

while [ $# -gt 0 ]; do
	arg="$1"
	shift
	case "$arg" in
		"-l")	# files included in initramfs - used by kbuild
			dep_list="$1"
			echo "deps_initramfs := \\" > $dep_list
			shift
			;;
		"-o")	# generate cpio image named $1
			output="$1"
			shift
			;;
		"-u")	# map $1 to uid=0 (root)
			root_uid="$1"
			[ "$root_uid" = "-1" ] && root_uid=$(id -u || echo 0)
			shift
			;;
		"-g")	# map $1 to gid=0 (root)
			root_gid="$1"
			[ "$root_gid" = "-1" ] && root_gid=$(id -g || echo 0)
			shift
			;;
		"-h")
			usage
			exit 0
			;;
		*)
			case "$arg" in
				"-"*)
					unknown_option
					;;
				*)	# input file/dir - process it
					input_file "$arg"
					;;
			esac
			;;
	esac
done

# If output_file is set we will generate cpio archive
# we are careful to delete tmp files
timestamp=
if test -n "$KBUILD_BUILD_TIMESTAMP"; then
	timestamp="$(date -d"$KBUILD_BUILD_TIMESTAMP" +%s || :)"
	if test -n "$timestamp"; then
		timestamp="-t $timestamp"
	fi
fi
usr/gen_init_cpio $timestamp $cpio_list > $output
