#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

PKG="Qt5Core Qt5Gui Qt5Widgets"

if [ -z "$(command -v pkg-config)" ]; then
	echo >&2 "*"
	echo >&2 "* 'make xconfig' requires 'pkg-config'. Please install it."
	echo >&2 "*"
	exit 1
fi

if pkg-config --exists $PKG; then
	echo cflags=\"-std=c++11 -fPIC $(pkg-config --cflags $PKG)\"
	echo libs=\"$(pkg-config --libs $PKG)\"
	echo moc=\"$(pkg-config --variable=host_bins Qt5Core)/moc\"
	exit 0
fi

echo >&2 "*"
echo >&2 "* Could not find Qt5 via pkg-config."
echo >&2 "* Please install Qt5 and make sure it's in PKG_CONFIG_PATH"
echo >&2 "*"
exit 1
