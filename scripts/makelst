#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# A script to dump mixed source code & assembly
# with correct relocations from System.map
# Requires the following lines in makefile:
#%.lst: %.c
#	$(CC) $(c_flags) -g -c -o $*.o $< &&
#	$(srctree)/scripts/makelst $*.o System.map $(OBJDUMP) > $@
#
# Copyright (C) 2000 IBM Corporation
# Author(s): DJ Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
#            William Stearns <wstearns@pobox.com>
#

# awk style field access
field() {
  shift $1 ; echo $1
}

t1=`$3 --syms $1 | grep .text | grep -m1 " F "`
if [ -n "$t1" ]; then
  t2=`field 6 $t1`
  if [ ! -r $2 ]; then
    echo "No System.map" >&2
  else
    t3=`grep $t2 $2`
    t4=`field 1 $t3`
    t5=`field 1 $t1`
    t6=`printf "%lu" $((0x$t4 - 0x$t5))`
  fi
fi
$3 -r --source --adjust-vma=${t6:-0} $1
