#!/bin/sh

# NAME:
#	stage-install.sh - wrapper around install
#
# SYNOPSIS:
#	stage-install.sh [variable="value"] "args" "dest"
#
# DESCRIPTION:
#	This script is a wrapper around the normal install(1).
#	Its role is to add '.dirdep' files to the destination.
#	The variables we might use are:
#
#	INSTALL
#		Path to actual install(1), default is
#		$REAL_INSTALL
#
#	OBJDIR
#		Path to the dir where '.dirdep' was generated,
#		default is '.'
#
#	_DIRDEP
#		Path to actual '.dirdep' file, default is
#		$OBJDIR/.dirdep
#
#	The "args" and "dest" are passed as is to install(1), and if a
#	'.dirdep' file exists it will be linked or copied to each
#	"file".dirdep placed in "dest" or "dest".dirdep if it happed
#	to be a file rather than a directory.
#
# SEE ALSO:
#	meta.stage.mk
#	

# RCSid:
# 	$FreeBSD$
#	$Id: stage-install.sh,v 1.5 2013/04/19 16:32:24 sjg Exp $
#
#	@(#) Copyright (c) 2013, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

INSTALL=${REAL_INSTALL:-install}
OBJDIR=.

while :
do
    case "$1" in
    *=*) eval "$1"; shift;;
    *) break;;
    esac
done

# if .dirdep doesn't exist, just run install and be done
_DIRDEP=${_DIRDEP:-$OBJDIR/.dirdep}
[ -s $_DIRDEP ] && EXEC= || EXEC=exec
$EXEC $INSTALL "$@" || exit 1

# from meta.stage.mk
LnCp() {
    rm -f $2 2> /dev/null
    ln $1 $2 2> /dev/null || cp -p $1 $2
}

StageDirdep() {
  t=$1
  if [ -s $t.dirdep ]; then
      cmp -s $_DIRDEP $t.dirdep && return
      echo "ERROR: $t installed by `cat $t.dirdep` not `cat $_DIRDEP`" >&2
      exit 1
  fi
  LnCp $_DIRDEP $t.dirdep || exit 1
}

args="$@"
while [ $# -gt 8 ]
do
    shift 8
done
eval dest=\$$#
if [ -f $dest ]; then
    # a file, there can be only one .dirdep needed
    StageDirdep $dest
elif [ -d $dest ]; then
    for f in $args
    do
        test -f $f || continue
        StageDirdep $dest/${f##*/}
    done
fi
