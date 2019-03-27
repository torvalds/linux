#! /bin/sh
#
# Copyright (c) 1999 Marcel Moolenaar
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
# 3. The name of the author may not be used to endorse or promote products
#    derived from this software without specific prior written permission
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

# parse install's options and ignore them completely.
dirmode=""
linkmode=""
while [ $# -gt 0 ]; do
    case $1 in
    -d) dirmode="YES"; shift;;
    -[bCcpSsUv]) shift;;
    -[BDfghMmNoT]) shift; shift;;
    -[BDfghMmNoT]*) shift;;
    -l)
	shift
	case $1 in
	*[sm]*) linkmode="symbolic";;	# XXX: 'm' should prefer hard
	*h*) linkmode="hard";;
	*) echo "invalid link mode"; exit 1;;
	esac
	shift
	;;
    *) break;
    esac
done

if [ "$#" -eq 0 ]; then
	echo "$0: no files/dirs specified" >&2
	exit 1
fi

if [ -z "$dirmode" ] && [ "$#" -lt 2 ]; then
	echo "$0: no target specified" >&2
	exit 1
fi

# the remaining arguments are assumed to be files/dirs only.
if [ -n "${linkmode}" ]; then
	if [ "${linkmode}" = "symbolic" ]; then
		ln -fsn "$@"
	else
		ln -f "$@"
	fi
elif [ -z "$dirmode" ]; then
	exec install -p "$@"
else
	exec install -d "$@"
fi
