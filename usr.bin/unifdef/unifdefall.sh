#!/bin/sh
#
# unifdefall: remove all the #if's from a source file
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2002 - 2013 Tony Finch <dot@dotat.at>
# Copyright (c) 2009 - 2010 Jonathan Nieder <jrnieder@gmail.com>
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
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

set -e

unifdef="$(dirname "$0")/unifdef"
if [ ! -e "$unifdef" ]
then
	unifdef=unifdef
fi

case "$@" in
"-d "*)	echo DEBUGGING 1>&2
	debug=-d
	shift
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/${0##*/}.XXXXXXXXXX") || exit 2
trap 'rm -r "$tmp" || exit 2' EXIT

export LC_ALL=C

# list of all controlling macros; assume these are undefined
"$unifdef" $debug -s "$@" | sort -u | sed 's/^/#undef /' >"$tmp/undefs"
# list of all macro definitions
cc -E -dM "$@" | sort >"$tmp/defs"

case $debug in
-d)	cat "$tmp/undefs" "$tmp/defs" 1>&2
esac

# order of -f arguments means definitions override undefs
"$unifdef" $debug -k -f "$tmp/undefs" -f "$tmp/defs" "$@"
