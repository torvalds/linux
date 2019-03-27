#!/bin/sh
# Ensure that zgrep -15 works.  Before gzip-1.5, it would fail.

# Copyright (C) 2012-2016 Free Software Foundation, Inc.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# limit so don't run it by default.

. "${srcdir=.}/init.sh"; path_prepend_ .

# A limited replacement for seq: handle 1 or 2 args; increment must be 1
seq()
{
  case $# in
    1) start=1  final=$1;;
    2) start=$1 final=$2;;
    *) echo you lose 1>&2; exit 1;;
  esac
  awk 'BEGIN{for(i='$start';i<='$final';i++) print i}' < /dev/null
}

seq 40 > in || framework_failure_
gzip < in > in.gz || framework_failure_
seq 2 32 > exp || framework_failure_

: ${GREP=grep}
$GREP -15 17 - < in > out && compare exp out || {
  echo >&2 "$0: $GREP does not support context options; skipping this test"
  exit 77
}

fail=0
zgrep -15 17 - < in.gz > out || fail=1
compare exp out || fail=1

Exit $fail
