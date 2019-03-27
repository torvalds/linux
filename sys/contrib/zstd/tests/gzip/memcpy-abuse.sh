#!/bin/sh
# Before gzip-1.4, this the use of memcpy in inflate_codes could
# mistakenly operate on overlapping regions.  Exercise that code.

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

# The input must be larger than 32KiB and slightly
# less uniform than e.g., all zeros.
printf wxy%032767d 0 | tee in | gzip > in.gz || framework_failure_

fail=0

# Before the fix, this would call memcpy with overlapping regions.
gzip -dc in.gz > out || fail=1

compare in out || fail=1

Exit $fail
