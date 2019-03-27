#!/bin/sh
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 1980, 1993
#	The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#       @(#)vgrind.sh	8.1 (Berkeley) 6/6/93
#
# $FreeBSD$
#

voptions=""
options=""
files=""
f=""
head=""
vf="/usr/libexec/vfontedpr"
tm="/usr/share/tmac"
postproc="psroff"

# Parse args
while test $# -gt 0; do
	case $1 in
	-f)
		f="filter"
		options="$options -f"
	;;
	-t)
		voptions="$voptions -t"
	;;
	-o*)
		voptions="$voptions $1"
	;;
	-W)
		voptions="$voptions -W"
	;;
	-d)
		if test $# -lt 2; then
			echo "$0: option $1 must have argument" >&2
			exit 1
		fi
		options="$options $1 $2"
		shift
	;;
	-h)
		if test $# -lt 2; then
			echo "$0: option $1 must have argument" >&2
			exit 1
		fi
		head="$2"
		shift
	;;
	-p)
		if test $# -lt 2; then
			echo "$0: option $1 must have argument" >&2
			exit 1
		fi
		postproc="$2"
		shift
	;;
	-*)
		options="$options $1"
	;;
	*)
		files="$files $1"
	;;
	esac
	shift
done

if test -r index; then
	echo > nindex
	for i in $files; do
		#       make up a sed delete command for filenames
		#       being careful about slashes.
		echo "? $i ?d" | sed -e "s:/:\\/:g" -e "s:?:/:g" >> nindex
	done
	sed -f nindex index > xindex
	if test "x$f" = xfilter; then
		if test "x$head" != x; then
			$vf $options -h "$head" $files
		else
			$vf $options $files
		fi | cat $tm/tmac.vgrind -
	else
		if test "x$head" != x; then
			$vf $options -h "$head" $files
		else
			$vf $options $files
		fi | sh -c "$postproc -rx1 $voptions -i -mvgrind 2>> xindex"
	fi
	sort -df -k 1,2 xindex > index
	rm nindex xindex
else
	if test "x$f" = xfilter; then
		if test "x$head" != x; then
			$vf $options -h "$head" $files
		else
			$vf $options $files
		fi | cat $tm/tmac.vgrind -
	else
		if test "x$head" != x; then
			$vf $options -h "$head" $files
		else
			$vf $options $files
		fi | $postproc -i $voptions -mvgrind
	fi
fi
