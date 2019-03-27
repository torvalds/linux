#!/bin/sh
#-
# Copyright (c) 2014 The FreeBSD Foundation
# All rights reserved.
#
# This software were developed by Glen Barber
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
# $FreeBSD$
#

set -C

PATH="/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin"
export PATH

usage() {
	echo "Usage:"
	echo -n "$(basename ${0}) [-rNNNNNN]"
	echo " [-l /path/for/output] /path/to/branch"
	echo " -r: The oldest commit to include in the search"
	echo ""
	exit 1
}

main() {
	while getopts "l:r:" arg ; do
		case ${arg} in
			l)
				# Disallow '-rNNNNNN' argument for oldest
				# revision # from becoming the log file
				# accidentally.
				where="${OPTARG##-r*}"
				[ -z "${where}" ] && usage
				if [ -e "${where}" ]; then
					echo "Log file already exists:"
					echo "  (${where})"
					return 2
				fi
				;;
			r)
				rev="${OPTARG##-r}"
				c=$(echo -n ${rev} | tr -d '0-9' | wc -c)
				if [ ${c} -ne 0 ]; then
					echo "Revision number must be numeric."
					return 2
				fi
				# Since the last specified revision is
				# specified, mangle the variable to
				# make svn syntax happy.
				rev="-r${rev}:rHEAD"
				;;
			*)
				usage
				;;
		esac
	done
	shift $(( ${OPTIND} - 1 ))

	# This assumes a local working copy, which svn search
	# allows exactly one repository path (although the root
	# can still be the path).
	[ "$#" -ne 1 ] && usage

	# If no log file, write to stdout.
	[ -z "${where}" ] && where=/dev/stdout

	svn=
	# Where is svn?
	for s in /usr/bin /usr/local/bin; do
		if [ -x ${s}/svn ]; then
			svn=${s}/svn
			break
		fi
		if [ -x ${s}/svnlite ]; then
			svn=${s}/svnlite
			break
		fi
	done
	# Did we find svn?
	if [ -z "${svn}" ]; then
		echo "svn(1) binary not found."
		return 2
	fi
	# Is more than one path specified?  (This should never
	# be triggered, because the argument count is checked
	# above, but better safe than sorry.)
	if [ $# -gt 1 ]; then
		echo "Cannot specify more than one working path."
		return 2
	fi
	# Does the directory exist?
	if [ ! -d "${1}" ]; then
		echo "Specified path (${1}) is not a directory."
		return 2
	fi
	# Is it a subversion repository checkout?
	${svn} info ${1} >/dev/null 2>&1
	if [ "$?" -ne 0 ]; then
		echo "Cannot determine svn repository information for ${1}"
		return 2
	fi

	# All tests passed.  Let's see what can possibly go wrong
	# from here.  The search string specified should match this
	# in PCRE speak: ':[\t ]*'
	${svn} log ${rev} --search 'Relnotes:*[A-Za-z0-9]*' ${1} > ${where}
	return $?
}

main "${@}"
exit $?
