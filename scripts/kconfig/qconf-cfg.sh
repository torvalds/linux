#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

PKG="Qt5Core Qt5Gui Qt5Widgets"

if [ -z "$(command -v ${HOSTPKG_CONFIG})" ]; then
	echo >&2 "*"
	echo >&2 "* 'make xconfig' requires '${HOSTPKG_CONFIG}'. Please install it."
	echo >&2 "*"
	exit 1
fi

if ${HOSTPKG_CONFIG} --exists $PKG; then
	echo cflags=\"-std=c++11 -fPIC $(${HOSTPKG_CONFIG} --cflags $PKG)\"
	echo libs=\"$(${HOSTPKG_CONFIG} --libs $PKG)\"
	echo moc=\"$(${HOSTPKG_CONFIG} --variable=host_bins Qt5Core)/moc\"
	exit 0
fi

echo >&2 "*"
echo >&2 "* Could not find Qt5 via ${HOSTPKG_CONFIG}."
echo >&2 "* Please install Qt5 and make sure it's in PKG_CONFIG_PATH"
echo >&2 "*"
exit 1
