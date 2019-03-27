#!/bin/sh
# Ensure that zgrep -f - works like grep -f -
# Before gzip-1.4, it would fail.

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

printf 'needle\nn2\n' > n || framework_failure_
cp n haystack || framework_failure_
gzip haystack || framework_failure_

fail=0
zgrep -f - haystack.gz < n > out 2>&1 || fail=1

compare out n || fail=1

if ${BASH_VERSION+:} false; then
  set +o posix
  # This failed with gzip 1.6.
  cat n n >nn || framework_failure_
  eval 'zgrep -h -f <(cat n) haystack.gz haystack.gz' >out || fail=1
  compare out nn || fail=1
fi

# This failed with gzip 1.4.
echo a-b | zgrep -e - > /dev/null || fail=1

Exit $fail
