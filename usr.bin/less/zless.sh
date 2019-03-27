#!/bin/sh
#
# $FreeBSD$
#

export LESSOPEN="||/usr/bin/lesspipe.sh %s"
exec /usr/bin/less "$@"
