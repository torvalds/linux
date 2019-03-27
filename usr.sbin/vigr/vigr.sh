#!/bin/sh
#-
# Copyright (c) 2014 Dag-Erling Smørgrav
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

error() {
	echo "$@" >&2
	exit 1
}

usage() {
	error "usage: vigr [-d dir]"
}

# Check arguments
while getopts d: opt ; do
	case $opt in
	d)
		etcdir="${OPTARG}"
		;;
	*)
		usage
		;;
	esac
done

# Look for the current group file
grpfile="${etcdir:-/etc}/group"
if [ ! -f "${grpfile}" ] ; then
	error "Missing group file"
fi

# Create a secure temporary working directory
tmpdir=$(mktemp -d -t vigr)
if [ -z "${tmpdir}" -o ! -d "${tmpdir}" ] ; then
	error "Unable to create the temporary directory"
fi
tmpfile="${tmpdir}/group"

# Clean up on exit
trap "exit 1" INT
trap "rm -rf '${tmpdir}'" EXIT
set -e

# Make a copy of the group file for the user to edit
cp "${grpfile}" "${tmpfile}"

while :; do
	# Let the user edit the file
	${EDITOR:-/usr/bin/vi} "${tmpfile}"

	# If the result is valid, install it and exit
	if chkgrp -q "${tmpfile}" ; then
		install -b -m 0644 -C -S "${tmpfile}" "${grpfile}"
		exit 0
	fi

	# If it is not, offer to re-edit
	while :; do
		echo -n "Re-edit the group file? "
		read ans
		case $ans in
		[Yy]|[Yy][Ee][Ss])
			break
			;;
		[Nn]|[Nn][Oo])
			exit 1
			;;
		esac
	done
done
