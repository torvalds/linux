#!/bin/sh
# $FreeBSD$

diff -I\$\FreeBSD $1 $2 | grep '^-' >/dev/null && printf "\tDIFFER: $1 $2\n" && exit 0 || exit 0
