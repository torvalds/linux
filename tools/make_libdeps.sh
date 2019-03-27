#!/bin/sh -e
#
# Copyright (c) 2002 Ruslan Ermilov, The FreeBSD Project
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

export PATH=/bin:/usr/bin

set -e

LC_ALL=C			# make sort deterministic
FS=': '				# internal field separator
LIBDEPENDS=./_libdeps		# intermediate output file
LIBDIRS=./_libdirs		# intermediate output file
USRSRC=${1:-/usr/src}		# source root
LIBS="
	lib
	gnu/lib
	kerberos5/lib
	secure/lib
	usr.bin/lex/lib
	cddl/lib
	contrib/ofed
"				# where to scan for libraries


# convert -lfoo to foo
convert()
{
    sed -e "s/\-l//g" -e "s/pthread/thr/g" -e "s/ncurses.*/ncurses/g"
}

# find library build directory given library name
findlibdir()
{
	while read NAME && read DIR
	do
		if [ "$NAME" = "$1" ]; then
			echo "$DIR"
			exit
		fi
	done

	# Should not happen
	echo lib_not_found/lib$1
}

# find library build directories given one or more library names
resolvelibdirs()
{
	while read LIBNAME
	do
		cat $LIBDIRS | tr ' ' '\n' | findlibdir "$LIBNAME"
	done
}

# Generate interdependencies between libraries.
#
genlibdepends()
{
	(
		# Reset file
		echo -n > $LIBDIRS

		# First pass - generate list of directories
		cd ${USRSRC}
		find -s ${LIBS} -name Makefile |
		xargs grep -l 'bsd\.lib\.mk' |
		while read makefile; do
			libdir=$(dirname ${makefile})
			libname=$(
				cd ${libdir}
				make -m ${USRSRC}/share/mk WITH_OFED=YES -V LIB
			)
			if [ "${libname}" ]; then
			    echo "${libname} ${libdir}" >> $LIBDIRS
			fi
		done

		# Second pass - generate dependencies
		find -s ${LIBS} -name Makefile |
		xargs grep -l 'bsd\.lib\.mk' |
		while read makefile; do
			libdir=$(dirname ${makefile})
			deps=$(
				cd ${libdir}
				make -m ${USRSRC}/share/mk WITH_OFED=YES -V LDADD
			)
			if [ "${deps}" ]; then
				echo ${libdir}"${FS}"$(echo ${deps} | tr ' ' '\n' | convert | resolvelibdirs)
			fi
		done
	)
}

main()
{
	if [ ! -f ${LIBDEPENDS} ]; then
		genlibdepends >${LIBDEPENDS}
	fi

	prebuild_libs=$(
		awk -F"${FS}" '{ print $2 }' ${LIBDEPENDS} | tr ' ' '\n' |
		    sort -u
	)
	echo "Libraries with dependents:"
	echo
	echo ${prebuild_libs} | tr ' ' '\n'
	echo

	echo "List of interdependencies:"
	echo
	for lib in ${prebuild_libs}; do
		grep "^${lib}${FS}" ${LIBDEPENDS} || true
	done |
	awk -F"${FS}" '{
		if ($2 in dependents)
			dependents[$2]=dependents[$2]" "$1
		else
			dependents[$2]=$1
	}
	END {
		for (lib in dependents)
			print dependents[lib]": " lib
	}' |
	sort

	exit 0
}

main
