#!/bin/sh
# gzip accepts trailing NUL bytes; don't fail if there is exactly one.
# Before gzip-1.4, this would fail.

# Copyright (C) 2009-2016 Free Software Foundation, Inc.

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

(echo 0 | gzip; printf '\0') > 0.gz || framework_failure_
(echo 00 | gzip; printf '\0\0') > 00.gz || framework_failure_
(echo 1 | gzip; printf '\1') > 1.gz || framework_failure_

fail=0

for i in 0 00 1; do
  gzip -d $i.gz; ret=$?
  test $ret -eq $i || fail=1
  test $ret = 1 && continue
  echo $i > exp || fail=1
  compare exp $i || fail=1
done

Exit $fail
