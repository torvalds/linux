#!/bin/sh
# $FreeBSD$
#
# This sample downloads the package digests.txz and packagesite.txz files from
# HTTP to /tmp (if they don't already exist) and then displays the package
# configuration/management screen using the local files (resulting in faster
# browsing of packages from-start since digests.txz/packagesite.txz can be
# loaded from local media).
#
# NOTE: Packages cannot be installed unless staged to
#       /tmp/packages/$PKG_ABI/All
#
[ "$_SCRIPT_SUBR" ] || . /usr/share/bsdconfig/script.subr || exit 1
nonInteractive=1
f_musthavepkg_init # Make sure we have a usable pkg(8) with $PKG_ABI
TMPDIR=/tmp
PKGDIR=$TMPDIR/packages/$PKG_ABI
[ -d "$PKGDIR" ] || mkdir -p "$PKGDIR" || exit 1
for file in digests.txz packagesite.txz; do
	[ -s "$PKGDIR/$file" ] && continue
	if [ ! "$HTTP_INITIALIZED" ]; then
		_httpPath=http://pkg.freebsd.org
		mediaSetHTTP
		mediaOpen
	fi
	f_show_info "Downloading %s from\n %s" "$file" "$_httpPath"
	f_device_get device_media "/$PKG_ABI/latest/$file" > $PKGDIR/$file ||
		exit 1
done
_directoryPath=$TMPDIR
mediaSetDirectory
configPackages
