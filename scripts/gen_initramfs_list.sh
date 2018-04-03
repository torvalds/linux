#!/bin/sh
# Copyright (C) Martin Schlemmer <azarah@nosferatu.za.org>
# Copyright (C) 2006 Sam Ravnborg <sam@ravnborg.org>
#
# Released under the terms of the GNU GPL
#
# Generate a cpio packed initramfs. It uses gen_init_cpio to generate
# the cpio archive, and then compresses it.
# The script may also be used to generate the inputfile used for gen_init_cpio
# This script assumes that gen_init_cpio is located in usr/ directory

# error out on errors
set -e

usage() {
cat << EOF
Usage:
$0 [-o <file>] [-u <uid>] [-g <gid>] {-d | <cpio_source>} ...
	-o <file>      Create compressed initramfs file named <file> using
		       gen_init_cpio and compressor depending on the extension
	-u <uid>       User ID to map to user ID 0 (root).
		       <uid> is only meaningful if <cpio_source> is a
		       directory.  "squash" forces all files to uid 0.
	-g <gid>       Group ID to map to group ID 0 (root).
		       <gid> is only meaningful if <cpio_source> is a
		       directory.  "squash" forces all files to gid 0.
	<cpio_source>  File list or directory for cpio archive.
		       If <cpio_source> is a .cpio file it will be used
		       as direct input to initramfs.
	-d             Output the default cpio list.

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

list_default_initramfs() {
	# echo usr/kinit/kinit
	:
}

default_initramfs() {
	cat <<-EOF >> ${output}
		# This is a very simple, default initramfs

		dir /dev 0755 0 0
		nod /dev/console 0600 0 0 c 5 1
		dir /root 0700 0 0
		# file /kinit usr/kinit/kinit 0755 0 0
		# slink /init kinit 0755 0 0
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

list_print_mtime() {
	:
}

print_mtime() {
	local my_mtime="0"

	if [ -e "$1" ]; then
		my_mtime=$(find "$1" -printf "%T@\n" | sort -r | head -n 1)
	fi

	echo "# Last modified: ${my_mtime}" >> ${output}
	echo "" >> ${output}
}

list_parse() {
	if [ -L "$1" ]; then
		return
	fi
	echo "$1" | sed 's/:/\\:/g; s/$/ \\/'
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
			local dev=`LC_ALL=C ls -l "${location}"`
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

	echo "${str}" >> ${output}

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

list_header() {
	:
}

header() {
	printf "\n#####################\n# $1\n" >> ${output}
}

# process one directory (incl sub-directories)
dir_filelist() {
	${dep_list}header "$1"

	srcdir=$(echo "$1" | sed -e 's://*:/:g')
	dirlist=$(find "${srcdir}" -printf "%p %m %U %G\n" | sort)

	# If $dirlist is only one line, then the directory is empty
	if [  "$(echo "${dirlist}" | wc -l)" -gt 1 ]; then
		${dep_list}print_mtime "$1"

		echo "${dirlist}" | \
		while read x; do
			${dep_list}parse ${x}
		done
	fi
}

# if only one file is specified and it is .cpio file then use it direct as fs
# if a directory is specified then add all files in given direcotry to fs
# if a regular file is specified assume it is in gen_initramfs format
input_file() {
	source="$1"
	if [ -f "$1" ]; then
		${dep_list}header "$1"
		is_cpio="$(echo "$1" | sed 's/^.*\.cpio\(\..*\)\{0,1\}/cpio/')"
		if [ $2 -eq 0 -a ${is_cpio} = "cpio" ]; then
			cpio_file=$1
			echo "$1" | grep -q '^.*\.cpio\..*' && is_cpio_compressed="compressed"
			[ ! -z ${dep_list} ] && echo "$1"
			return 0
		fi
		if [ -z ${dep_list} ]; then
			print_mtime "$1" >> ${output}
			cat "$1"         >> ${output}
		else
		        echo "$1 \\"
			cat "$1" | while read type dir file perm ; do
				if [ "$type" = "file" ]; then
					echo "$file \\";
				fi
			done
		fi
	elif [ -d "$1" ]; then
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
cpio_file=
cpio_list=
output="/dev/stdout"
output_file=""
is_cpio_compressed=
compr="gzip -n -9 -f"

arg="$1"
case "$arg" in
	"-l")	# files included in initramfs - used by kbuild
		dep_list="list_"
		echo "deps_initramfs := $0 \\"
		shift
		;;
	"-o")	# generate compressed cpio image named $1
		shift
		output_file="$1"
		cpio_list="$(mktemp ${TMPDIR:-/tmp}/cpiolist.XXXXXX)"
		output=${cpio_list}
		echo "$output_file" | grep -q "\.gz$" \
                && [ -x "`which gzip 2> /dev/null`" ] \
                && compr="gzip -n -9 -f"
		echo "$output_file" | grep -q "\.bz2$" \
                && [ -x "`which bzip2 2> /dev/null`" ] \
                && compr="bzip2 -9 -f"
		echo "$output_file" | grep -q "\.lzma$" \
                && [ -x "`which lzma 2> /dev/null`" ] \
                && compr="lzma -9 -f"
		echo "$output_file" | grep -q "\.xz$" \
                && [ -x "`which xz 2> /dev/null`" ] \
                && compr="xz --check=crc32 --lzma2=dict=1MiB"
		echo "$output_file" | grep -q "\.lzo$" \
                && [ -x "`which lzop 2> /dev/null`" ] \
                && compr="lzop -9 -f"
		echo "$output_file" | grep -q "\.lz4$" \
                && [ -x "`which lz4 2> /dev/null`" ] \
                && compr="lz4 -l -9 -f"
		echo "$output_file" | grep -q "\.cpio$" && compr="cat"
		shift
		;;
esac
while [ $# -gt 0 ]; do
	arg="$1"
	shift
	case "$arg" in
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
		"-d")	# display default initramfs list
			default_list="$arg"
			${dep_list}default_initramfs
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
					input_file "$arg" "$#"
					;;
			esac
			;;
	esac
done

# If output_file is set we will generate cpio archive and compress it
# we are careful to delete tmp files
if [ ! -z ${output_file} ]; then
	if [ -z ${cpio_file} ]; then
		timestamp=
		if test -n "$KBUILD_BUILD_TIMESTAMP"; then
			timestamp="$(date -d"$KBUILD_BUILD_TIMESTAMP" +%s || :)"
			if test -n "$timestamp"; then
				timestamp="-t $timestamp"
			fi
		fi
		cpio_tfile="$(mktemp ${TMPDIR:-/tmp}/cpiofile.XXXXXX)"
		usr/gen_init_cpio $timestamp ${cpio_list} > ${cpio_tfile}
	else
		cpio_tfile=${cpio_file}
	fi
	rm ${cpio_list}
	if [ "${is_cpio_compressed}" = "compressed" ]; then
		cat ${cpio_tfile} > ${output_file}
	else
		(cat ${cpio_tfile} | ${compr}  - > ${output_file}) \
		|| (rm -f ${output_file} ; false)
	fi
	[ -z ${cpio_file} ] && rm ${cpio_tfile}
fi
exit 0
