#!/bin/sh
# Before gzip-1.5, gzip -d -S '' k.gz would delete F.gz and not create "F"

# Copyright (C) 2010-2016 Free Software Foundation, Inc.

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

printf anything | gzip > F.gz || framework_failure_
echo y > yes || framework_failure_
echo "gzip: invalid suffix ''" > expected-err || framework_failure_

fail=0

gzip ---presume-input-tty -d -S '' F.gz < yes > out 2>err && fail=1

compare /dev/null out || fail=1
compare expected-err err || fail=1

test -f F.gz || fail=1

Exit $fail
