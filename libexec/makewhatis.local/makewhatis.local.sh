#!/bin/sh
#
# Copyright (c) April 1996 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
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
# makewhatis.local - start makewhatis(1) only for file systems 
#		     physically mounted on the system
#
# Running makewhatis from /etc/periodic/weekly/320.whatis for rw nfs-mounted
# /usr may kill your NFS server -- all clients start makewhatis at the same
# time! So use this wrapper instead calling makewhatis directly.
#
# $FreeBSD$

PATH=/bin:/usr/bin:$PATH; export PATH
opt= dirs= localdirs=

for arg
do
	case "$arg" in
		-*) 	opt="$opt $arg";;
		*)	dirs="$dirs $arg";;
	esac
done

dirs=`echo $dirs | sed 's/:/ /g'`
case X"$dirs" in X) echo "usage: $0 [options] directories ..."; exit 1;; esac

localdirs=`find -H $dirs -fstype local \! -fstype rdonly -type d -prune -print`

case X"$localdirs" in
	X) 	echo "$0: no local rw-mounted manual directories found: $dirs"
		exit 1;;
	*) 	exec `basename $0 .local` $opt $localdirs;;
esac
