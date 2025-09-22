#!/bin/sh
#
# Copyright (c) 2015 Ingo Schwarze <schwarze@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

test_ps()
{
	ps_args=$1
	expected=$2
	result=`ps -p $pid $ps_args | tail -n +2`
	if [ "$result" != "$expected" ]; then
		echo "$ps_vars ps $ps_args"
		echo "expected: >$expected<"
		echo "result:   >$result<"
		exit 1;
	fi
}

# path shorter than 40 bytes (hopefully)
./shortsleep &
pid=$!
dirname=`readlink -fn . | cut -c -40`
padded=`printf '%-40s' $dirname`

test_ps "-o cwd,command" "$padded ./shortsleep"
test_ps "-wwo cwd,command" "$padded ./shortsleep"
test_ps "-o cwd" "$dirname"
test_ps "-wwo cwd" "$dirname"
test_ps "-wwo command,cwd" "./shortsleep     $dirname"

kill $pid

# path longer than 40 bytes
rm -rf ridiculously_long_directory_name_component
mkdir ridiculously_long_directory_name_component
cd ridiculously_long_directory_name_component

../shortsleep &
pid=$!
dirname=`readlink -fn . | cut -c -40`

test_ps "-o cwd,command" "$dirname ../shortsleep"
test_ps "-wwo cwd,command" "$dirname ../shortsleep"
test_ps "-o cwd" "$dirname"
test_ps "-wwo cwd" "$dirname"
test_ps "-wwo command,cwd" "../shortsleep    $dirname"

kill $pid
cd ..
rm -rf ridiculously_long_directory_name_component

exit 0
