#!/bin/sh
# Before gzip-1.4, gzip -d would segfault on some inputs.

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

# This test case was provided by Aki Helin.
printf '\037\235\220\0\0\0\304' > helin.gz || framework_failure_
printf '\0\0' > exp || framework_failure_

fail=0

gzip -dc helin.gz > out || fail=1
compare exp out || fail=1

Exit $fail
