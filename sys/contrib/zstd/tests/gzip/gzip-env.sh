#!/bin/sh
# Test the obsolescent GZIP environment variable.

# Copyright 2015-2016 Free Software Foundation, Inc.

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

#echo PATH=$PATH
#gzip --version

echo a >exp || framework_failure_
gzip <exp >in || framework_failure_

fail=0
GZIP=-qv gzip -d <in >out 2>err || fail=1
compare exp out || fail=1

for badopt in -- -c --stdout -d --decompress -f --force -h --help -k --keep \
  -l --list -L --license -r --recursive -Sxxx --suffix=xxx '--suffix xxx' \
  -t --test -V --version
do
  GZIP=$badopt gzip -d <in >out 2>err && fail=1
done

for goodopt in -n --no-name -N --name -q --quiet -v --verbose \
  -1 --fast -2 -3 -4 -5 -6 -7 -8 -9 --best
do
  GZIP=$goodopt gzip -d <in >out 2>err || fail=1
  compare exp out || fail=1
done

Exit $fail
