#!/bin/sh
# Exercise the --keep option.

# Copyright (C) 2013-2016 Free Software Foundation, Inc.

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

echo fooooooooo > in || framework_failure_
cp in orig || framework_failure_

fail=0

# Compress and decompress both with and without --keep.
for k in --keep ''; do
  # With --keep, the source must be retained, otherwise, it must be removed.
  case $k in --keep) op='||' ;; *) op='&&' ;; esac

  gzip $k in || fail=1
  eval "test -f in $op fail=1"
  test -f in.gz || fail=1
  rm -f in || fail=1

  gzip -d $k in.gz || fail=1
  eval "test -f in.gz $op fail=1"
  test -f in || fail=1
  compare in orig || fail=1
  rm -f in.gz || fail=1
done

cp orig in || framework_failure_
log=$(gzip -kv in 2>&1) || fail=1
case $log in
  *'created in.gz'*) ;;
  *) fail=1;;
esac

Exit $fail
