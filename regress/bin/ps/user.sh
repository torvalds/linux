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

./shortsleep &
pid=$!
login=`id -p | awk '$1=="login"{l=$2} $1=="uid"&&l==""{l=$2} END{print substr(l,1,32)}'`
uname=`id -un | cut -c -8`
gname=`id -gn | cut -c -8`
lpad=`printf '%-32s' $login`
upad=`printf '%-8s' $uname`
gpad=`printf '%-8s' $gname`

test_ps "-o login,user,ruser,group,rgroup" "$lpad $upad $upad $gpad $gname"
test_ps "-o group,rgroup,login,user,ruser" "$gpad $gpad $lpad $upad $uname"
test_ps "-o user,ruser,group,rgroup,login" "$upad $upad $gpad $gpad $login"

kill $pid
exit 0
