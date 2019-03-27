#!/bin/sh
# Check that zgrep is terminated gracefully by signal when
# its grep/sed pipeline is terminated by a signal.

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

echo a | gzip -c > f.gz || framework_failure_

test "x$PERL" = x && PERL=perl
("$PERL" -e 'use POSIX qw(dup2)') >/dev/null 2>&1 ||
   skip_ "no suitable perl found"

# Run the arguments as a command, in a process where stdout is a
# dangling pipe and SIGPIPE has the default signal-handling action.
# This can't be done portably in the shell, because if SIGPIPE is
# ignored when the shell is entered, the shell might refuse to trap
# it.  Fall back on Perl+POSIX, if available.  Take care to close the
# pipe's read end before running the program; the equivalent of the
# shell's "command | :" has a race condition in that COMMAND could
# write before ":" exits.
write_to_dangling_pipe () {
  program=${1?}
  shift
  args=
  for arg; do
    args="$args, '$arg'"
  done
  "$PERL" -e '
     use POSIX qw(dup2);
     $SIG{PIPE} = "DEFAULT";
     pipe my ($read_end, $write_end) or die "pipe: $!\n";
     dup2 fileno $write_end, 1 or die "dup2: $!\n";
     close $read_end or die "close: $!\n";
     exec '"'$program'$args"';
  '
}

write_to_dangling_pipe cat f.gz f.gz
signal_status=$?
test 128 -lt $signal_status ||
  framework_failure_ 'signal handling busted on this host'

fail=0

write_to_dangling_pipe zgrep a f.gz f.gz
test $? -eq $signal_status || fail=1

Exit $fail
