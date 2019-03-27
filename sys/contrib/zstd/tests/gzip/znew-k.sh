#!/bin/sh
# Check that znew -K works without compress(1).

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

cat <<'EOF' >compress || framework_failure_
#!/bin/sh
echo >&2 'compress has been invoked'
exit 1
EOF
chmod +x compress || framework_failure_

# Note that the basename must have a length of 6 or greater.
# Otherwise, "test -f $name" below would fail.
name=123456.Z

printf '%1012977s' ' ' | gzip -c > $name || framework_failure_

fail=0

znew -K $name || fail=1
test -f $name || fail=1

Exit $fail
