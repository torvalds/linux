#!/bin/sh
# $FreeBSD$
#
# This sample installs a short list of packages from the main HTTP site.
#
[ "$_SCRIPT_SUBR" ] || . /usr/share/bsdconfig/script.subr || exit 1
nonInteractive=1
_httpPath=http://pkg.freebsd.org
mediaSetHTTP
mediaOpen
for package in wget bash rsync; do
	packageAdd
done
