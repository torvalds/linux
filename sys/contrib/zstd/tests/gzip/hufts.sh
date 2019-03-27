#!/bin/sh
# Exercise a bug whereby an invalid input could make gzip -d misbehave.

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

printf '\n...: invalid compressed data--format violated\n' > exp \
  || framework_failure_

fail=0
gzip -dc "$abs_srcdir/hufts-segv.gz" > out 2> err
test $? = 1 || fail=1

compare /dev/null out || fail=1

sed 's/.*hufts-segv.gz: /...: /' err > k; mv k err || fail=1
compare exp err || fail=1

Exit $fail
