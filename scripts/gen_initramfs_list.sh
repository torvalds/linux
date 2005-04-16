#!/bin/bash
# Copyright (C) Martin Schlemmer <azarah@nosferatu.za.org>
# Released under the terms of the GNU GPL
#
# Generate a newline separated list of entries from the file/directory
# supplied as an argument.
#
# If a file/directory is not supplied then generate a small dummy file.
#
# The output is suitable for gen_init_cpio built from usr/gen_init_cpio.c.
#

default_initramfs() {
	cat <<-EOF
		# This is a very simple, default initramfs

		dir /dev 0755 0 0
		nod /dev/console 0600 0 0 c 5 1
		dir /root 0700 0 0
	EOF
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
	local argv1="$1"
	local my_mtime="0"

	if [ -e "${argv1}" ]; then
		my_mtime=$(find "${argv1}" -printf "%T@\n" | sort -r | head -n 1)
	fi
	
	echo "# Last modified: ${my_mtime}"
	echo
}

parse() {
	local location="$1"
	local name="${location/${srcdir}//}"
	# change '//' into '/'
	name="${name//\/\///}"
	local mode="$2"
	local uid="$3"
	local gid="$4"
	local ftype=$(filetype "${location}")
	# remap uid/gid to 0 if necessary
	[ "$uid" -eq "$root_uid" ] && uid=0
	[ "$gid" -eq "$root_gid" ] && gid=0
	local str="${mode} ${uid} ${gid}"

	[ "${ftype}" == "invalid" ] && return 0
	[ "${location}" == "${srcdir}" ] && return 0

	case "${ftype}" in
		"file")
			str="${ftype} ${name} ${location} ${str}"
			;;
		"nod")
			local dev_type=
			local maj=$(LC_ALL=C ls -l "${location}" | \
					gawk '{sub(/,/, "", $5); print $5}')
			local min=$(LC_ALL=C ls -l "${location}" | \
					gawk '{print $6}')

			if [ -b "${location}" ]; then
				dev_type="b"
			else
				dev_type="c"
			fi
			str="${ftype} ${name} ${str} ${dev_type} ${maj} ${min}"
			;;
		"slink")
			local target=$(LC_ALL=C ls -l "${location}" | \
					gawk '{print $11}')
			str="${ftype} ${name} ${target} ${str}"
			;;
		*)
			str="${ftype} ${name} ${str}"
			;;
	esac

	echo "${str}"

	return 0
}

usage() {
	printf    "Usage:\n"
	printf    "$0 [ [-u <root_uid>] [-g <root_gid>] [-d | <cpio_source>] ] . . .\n"
	printf    "\n"
	printf -- "-u <root_uid>  User ID to map to user ID 0 (root).\n"
	printf    "               <root_uid> is only meaningful if <cpio_source>\n"
	printf    "               is a directory.\n"
	printf -- "-g <root_gid>  Group ID to map to group ID 0 (root).\n"
	printf    "               <root_gid> is only meaningful if <cpio_source>\n"
	printf    "               is a directory.\n"
	printf    "<cpio_source>  File list or directory for cpio archive.\n"
	printf    "               If <cpio_source> is not provided then a\n"
	printf    "               a default list will be output.\n"
	printf -- "-d             Output the default cpio list.  If no <cpio_source>\n"
	printf    "               is given then the default cpio list will be output.\n"
	printf    "\n"
	printf    "All options may be repeated and are interpreted sequentially\n"
	printf    "and immediately.  -u and -g states are preserved across\n"
	printf    "<cpio_source> options so an explicit \"-u 0 -g 0\" is required\n"
	printf    "to reset the root/group mapping.\n"
}

build_list() {
	printf "\n#####################\n# $cpio_source\n"

	if [ -f "$cpio_source" ]; then
		print_mtime "$cpio_source"
		cat "$cpio_source"
	elif [ -d "$cpio_source" ]; then
		srcdir=$(echo "$cpio_source" | sed -e 's://*:/:g')
		dirlist=$(find "${srcdir}" -printf "%p %m %U %G\n" 2>/dev/null)

		# If $dirlist is only one line, then the directory is empty
		if [  "$(echo "${dirlist}" | wc -l)" -gt 1 ]; then
			print_mtime "$cpio_source"
		
			echo "${dirlist}" | \
			while read x; do
				parse ${x}
			done
		else
			# Failsafe in case directory is empty
			default_initramfs
		fi
	else
		echo "  $0: Cannot open '$cpio_source'" >&2
		exit 1
	fi
}


root_uid=0
root_gid=0

while [ $# -gt 0 ]; do
	arg="$1"
	shift
	case "$arg" in
		"-u")
			root_uid="$1"
			shift
			;;
		"-g")
			root_gid="$1"
			shift
			;;
		"-d")
			default_list="$arg"
			default_initramfs
			;;
		"-h")
			usage
			exit 0
			;;
		*)
			case "$arg" in
				"-"*)
					printf "ERROR: unknown option \"$arg\"\n" >&2
					printf "If the filename validly begins with '-', then it must be prefixed\n" >&2
					printf "by './' so that it won't be interpreted as an option." >&2
					printf "\n" >&2
					usage >&2
					exit 1
					;;
				*)
					cpio_source="$arg"
					build_list
					;;
			esac
			;;
	esac
done

# spit out the default cpio list if a source hasn't been specified
[ -z "$cpio_source" -a -z "$default_list" ] && default_initramfs

exit 0
