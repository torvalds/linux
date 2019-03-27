#!/bin/sh
# Exercise zdiff with two compressed inputs.
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

echo a > a || framework_failure_
echo b > b || framework_failure_
gzip a b || framework_failure_

cat <<EOF > exp
1c1
< a
---
> b
EOF

fail=0
zdiff a.gz b.gz > out 2>&1
test $? = 1 || fail=1

compare exp out || fail=1

rm -f out
# expect success, for equal files
zdiff a.gz a.gz > out 2> err || fail=1
# expect no output
test -s out && fail=1
# expect no stderr
test -s err && fail=1

Exit $fail
