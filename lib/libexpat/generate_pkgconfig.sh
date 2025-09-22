#!/bin/sh
#
# $OpenBSD: generate_pkgconfig.sh,v 1.2 2012/07/07 08:25:21 jasper Exp $
#
# Copyright (c) 2010-2012 Jasper Lievisse Adriaanse <jasper@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
# Generate pkg-config file for expat.

usage() {
	echo "usage: ${0##*/} -c current_directory -o obj_directory"
	exit 1
}

curdir=
objdir=
while getopts "c:o:" flag; do
	case "$flag" in
		c)
			curdir=$OPTARG
			;;
		o)
			objdir=$OPTARG
			;;
		*)
			usage
			;;
	esac
done

[ -n "${curdir}" ] || usage
if [ ! -d "${curdir}" ]; then
	echo "${0##*/}: ${curdir}: not found"
	exit 1
fi
[ -n "${objdir}" ] || usage
if [ ! -w "${objdir}" ]; then
	echo "${0##*/}: ${objdir}: not found or not writable"
	exit 1
fi

version_major_re="s/#define[[:blank:]]XML_MAJOR_VERSION[[:blank:]](.*)/\1/p"
version_minor_re="s/#define[[:blank:]]XML_MINOR_VERSION[[:blank:]](.*)/\1/p"
version_micro_re="s/#define[[:blank:]]XML_MICRO_VERSION[[:blank:]](.*)/\1/p"
version_file=${curdir}/lib/expat.h
lib_version=$(sed -nE ${version_major_re} ${version_file}).$(sed -nE ${version_minor_re} ${version_file}).$(sed -nE ${version_micro_re} ${version_file})

pc_file="${objdir}/expat.pc"
cat > ${pc_file} << __EOF__
prefix=/usr
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: expat
Description: expat XML parser
URL: http://www.libexpat.org
Version: ${lib_version}
Libs: -L\${libdir} -lexpat
Cflags: -I\${includedir}
__EOF__
