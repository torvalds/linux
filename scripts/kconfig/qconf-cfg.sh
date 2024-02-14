#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

cflags=$1
libs=$2
bin=$3

PKG5="Qt5Core Qt5Gui Qt5Widgets"
PKG6="Qt6Core Qt6Gui Qt6Widgets"

if [ -z "$(command -v ${HOSTPKG_CONFIG})" ]; then
	echo >&2 "*"
	echo >&2 "* 'make xconfig' requires '${HOSTPKG_CONFIG}'. Please install it."
	echo >&2 "*"
	exit 1
fi

if ${HOSTPKG_CONFIG} --exists $PKG6; then
	${HOSTPKG_CONFIG} --cflags ${PKG6} > ${cflags}
	# Qt6 requires C++17.
	echo -std=c++17 >> ${cflags}
	${HOSTPKG_CONFIG} --libs ${PKG6} > ${libs}
	${HOSTPKG_CONFIG} --variable=libexecdir Qt6Core > ${bin}
	exit 0
fi

if ${HOSTPKG_CONFIG} --exists $PKG5; then
	${HOSTPKG_CONFIG} --cflags ${PKG5} > ${cflags}
	${HOSTPKG_CONFIG} --libs ${PKG5} > ${libs}
	${HOSTPKG_CONFIG} --variable=host_bins Qt5Core > ${bin}
	exit 0
fi

echo >&2 "*"
echo >&2 "* Could not find Qt6 or Qt5 via ${HOSTPKG_CONFIG}."
echo >&2 "* Please install Qt6 or Qt5 and make sure it's in PKG_CONFIG_PATH"
echo >&2 "* You need $PKG6 for Qt6"
echo >&2 "* You need $PKG5 for Qt5"
echo >&2 "*"
exit 1
