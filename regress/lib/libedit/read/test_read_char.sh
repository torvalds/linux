#!/bin/sh
# $OpenBSD: test_read_char.sh,v 1.3 2017/07/05 15:31:45 bluhm Exp $
#
# Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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

testrc()
{
	stdin=$1
	e_utf8=$2
	e_ascii=$3
	[ -n "$e_ascii" ] || e_ascii=$e_utf8
	result=`echo -n "$stdin" | LC_CTYPE=en_US.UTF-8 ./test_read_char`
	if [ "$result" != "${e_utf8}" ]; then
		echo "input:    >>>$stdin<<<"
		echo "expected: >>>$e_utf8<<< UTF-8"
		echo "result:   >>>$result<<<"
		exit 1;
	fi
	result=`echo -n "$stdin" | LC_CTYPE=C ./test_read_char`
	if [ "$result" != "${e_ascii}" ]; then
		echo "input:    >>>$stdin<<<"
		echo "expected: >>>$e_ascii<<< ASCII"
		echo "result:   >>>$result<<<"
		exit 1;
	fi
}

# Valid ASCII.
testrc "" "0."
testrc "a" "61,0."
testrc "ab" "61,62,0."

# Valid UTF-8.
testrc "\0303\0251" "e9,0." "c3,a9,0."

# Incomplete UTF-8.
testrc "\0303" "0." "c3,0."
testrc "\0303a" "*61,0." "c3,61,0."
testrc "\0303ab" "*61,62,0." "c3,61,62,0."

# UTF-16 surrogate.
testrc "\0355\0277\0277ab" "*61,62,0." "ed,bf,bf,61,62,0."

# Isolated UTF-8 continuation bytes.
testrc "\0200" "*0." "80,0."
testrc "\0200ab" "*61,62,0." "80,61,62,0."
testrc "a\0200bc" "61,*62,63,0." "61,80,62,63,0."
testrc "\0200\0303\0251" "*e9,0." "80,c3,a9,0."
testrc "\0200\0303ab" "*61,62,0." "80,c3,61,62,0."

# Invalid bytes.
testrc "\0377ab" "*61,62,0." "ff,61,62,0."
testrc "\0355\0277\0377ab" "*61,62,0." "ed,bf,ff,61,62,0."

exit 0
