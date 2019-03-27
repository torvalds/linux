#!/bin/sh
# gzip should report invalid 'unpack' input when uncompressing.
# With gzip-1.5, it would output invalid data instead.

# Copyright (C) 2012-2016 Free Software Foundation, Inc.

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

for input in \
  '\037\036\000\000\037\213\010\000\000\000\000\000\002\003\036\000\000\000\002\003\037\213\010\000\000\000\000\000\002\003\355\301\001\015\000\000\000\302\240\037\000\302\240\037\213\010\000\000\000\000\000\002\003\355\301' \
  '\037\213\010\000\000\000\000\000\002\003\355\301\001\015\000\000\000\302\240\076\366\017\370\036\016\030\000\000\000\000\000\000\000\000\000\034\010\105\140\104\025\020\047\000\000\037\036\016\030\000\000\000'; do

  printf "$input" >in || framework_failure_

  if gzip -d <in >out 2>err; then
    fail=1
  else
    fail=0
  fi
done

Exit $fail
