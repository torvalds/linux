#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

cflags=$1
libs=$2

PKG="ncursesw"
PKG2="ncurses"

if [ -n "$(command -v ${HOSTPKG_CONFIG})" ]; then
	if ${HOSTPKG_CONFIG} --exists $PKG; then
		${HOSTPKG_CONFIG} --cflags ${PKG} > ${cflags}
		${HOSTPKG_CONFIG} --libs ${PKG} > ${libs}
		exit 0
	fi

	if ${HOSTPKG_CONFIG} --exists ${PKG2}; then
		${HOSTPKG_CONFIG} --cflags ${PKG2} > ${cflags}
		${HOSTPKG_CONFIG} --libs ${PKG2} > ${libs}
		exit 0
	fi
fi

# Check the default paths in case pkg-config is not installed.
# (Even if it is installed, some distributions such as openSUSE cannot
# find ncurses by pkg-config.)
if [ -f /usr/include/ncursesw/ncurses.h ]; then
	echo -D_GNU_SOURCE -I/usr/include/ncursesw > ${cflags}
	echo -lncursesw > ${libs}
	exit 0
fi

if [ -f /usr/include/ncurses/ncurses.h ]; then
	echo -D_GNU_SOURCE -I/usr/include/ncurses > ${cflags}
	echo -lncurses > ${libs}
	exit 0
fi

# As a final fallback before giving up, check if $HOSTCC knows of a default
# ncurses installation (e.g. from a vendor-specific sysroot).
if echo '#include <ncurses.h>' | ${HOSTCC} -E - >/dev/null 2>&1; then
	echo -D_GNU_SOURCE > ${cflags}
	echo -lncurses > ${libs}
	exit 0
fi

echo >&2 "*"
echo >&2 "* Unable to find the ncurses package."
echo >&2 "* Install ncurses (ncurses-devel or libncurses-dev"
echo >&2 "* depending on your distribution)."
echo >&2 "*"
echo >&2 "* You may also need to install ${HOSTPKG_CONFIG} to find the"
echo >&2 "* ncurses installed in a non-default location."
echo >&2 "*"
exit 1
