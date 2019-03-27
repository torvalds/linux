#!/bin/sh
# $FreeBSD$

diff $1 $2 | grep '^-' >/dev/null && echo DIFFER: $1 $2 && exit 0 || exit 0
