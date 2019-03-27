#!/bin/sh
#
# Copyright (c) 2011 Max Khon, The FreeBSD Project
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

#
# Utility script to build specific parts of the source tree on all arches
#
# Example:
#
# cd /usr/src
# make toolchains		# build toolchain for all arches
# sh tools/tinder.sh gnu/lib/libdialog usr.sbin/sade NO_CLEAN=yes
#				# build libdialog and sade for all architectures
#				# without making clean
# sh tools/tinder.sh gnu/lib/libdialog usr.sbin/sade TARGETS="amd64 i386"
#				# build libdialog and sade only for amd64 and i386
#

if [ $# -eq 0 ]; then
	echo 1>&2 "Usage: `basename $0` [MAKEVAR=value...] path..."
	exit 1
fi

# MAKE_ARGS is intentionally not reset to allow caller to specify additional MAKE_ARGS
SUBDIR=
for i in "$@"; do
	case "$i" in
	*=*)
		MAKE_ARGS="$MAKE_ARGS $i"
		;;
	*)
		SUBDIR="$SUBDIR $i"
		;;
	esac
done
make tinderbox UNIVERSE_TARGET="_cleanobj _obj everything" $MAKE_ARGS SUBDIR_OVERRIDE="$SUBDIR"
