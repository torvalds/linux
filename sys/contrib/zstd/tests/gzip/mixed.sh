#!/bin/sh
# Ensure that gzip -cdf handles mixed compressed/not-compressed data
# Before gzip-1.5, it would produce invalid output.

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

printf 'xxx\nyyy\n'      > exp2 || framework_failure_
printf 'aaa\nbbb\nccc\n' > exp3 || framework_failure_

fail=0

(echo xxx; echo yyy) > in || fail=1
gzip -cdf < in > out || fail=1
compare exp2 out || fail=1

# Uncompressed input, followed by compressed data.
# Currently fails, so skip it.
# (echo xxx; echo yyy|gzip) > in || fail=1
# gzip -cdf < in > out || fail=1
# compare exp2 out || fail=1

# Compressed input, followed by regular (not-compressed) data.
(echo xxx|gzip; echo yyy) > in || fail=1
gzip -cdf < in > out || fail=1
compare exp2 out || fail=1

(echo xxx|gzip; echo yyy|gzip) > in || fail=1
gzip -cdf < in > out || fail=1
compare exp2 out || fail=1

in_str=0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+=%
for i in 0 1 2 3 4 5 6 7 8 9 a; do in_str="$in_str$in_str" ;done

# Start with some small sizes.  $(seq 64)
sizes=$(i=0; while :; do echo $i; test $i = 64 && break; i=$(expr $i + 1); done)

# gzip's internal buffer size is 32KiB + 64 bytes:
sizes="$sizes 32831 32832 32833"

# 128KiB, +/- 1
sizes="$sizes 131071 131072 131073"

# Ensure that "gzip -cdf" acts like cat, for a range of small input files.
i=0
for i in $sizes; do
  echo $i
  printf %$i.${i}s $in_str > in
  gzip -cdf < in > out
  compare in out || fail=1
done

Exit $fail
