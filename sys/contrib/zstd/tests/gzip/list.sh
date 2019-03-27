#!/bin/sh
# Exercise the --list option.

# Copyright 2016 Free Software Foundation, Inc.

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

echo zoology zucchini > in || framework_failure_
cp in orig || framework_failure_

gzip -l in && fail=1
gzip -9 in || fail=1
gzip -l in.gz >out1 || fail=1
gzip -l in.gz | cat >out2 || fail=1
compare out1 out2 || fail=1

Exit $fail
