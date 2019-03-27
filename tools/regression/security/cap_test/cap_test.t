#!/bin/sh
#
# $FreeBSD$
#

if test -z "${DIR}" ; then DIR=$( make -V .OBJDIR ); fi
if test -z "${DIR}" ; then DIR=$( dirname $0 ); fi

make > /dev/null || exit 1
$DIR/cap_test $*

