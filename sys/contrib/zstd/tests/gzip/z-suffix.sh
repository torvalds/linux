#!/bin/sh
# Check that -Sz works.

# Copyright 2014-2016 Free Software Foundation, Inc.

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

printf anything > F && cp F G || framework_failure_
gzip -Sz F || fail=1
test ! -f F || fail=1
test -f Fz || fail=1
gzip -dSz F || fail=1
test ! -f Fz || fail=1
compare F G || fail\1

Exit $fail
