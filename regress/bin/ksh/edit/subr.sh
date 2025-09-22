# $OpenBSD: subr.sh,v 1.9 2021/07/10 07:10:31 anton Exp $
#
# Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
# Copyright (c) 2017 Anton Lindqvist <anton@openbsd.org>
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

testseq() {
	stdin=$1
	exp=$(echo "$2")
	act=$(echo -n "$stdin" | ./edit -p "$PS1" -- ${KSH:-/bin/ksh} -r 2>&1)
	[ $? = 0 ] && [ "$exp" = "$act" ] && return 0

	dump input "$stdin"
	dump expected "$exp"
	dump actual "$act"

	exit 1
}

dump() {
	printf '%s:\n>>>%s<<<\n' "$1" "$(echo -n "$2" | vis -ol)"
	echo -n "$2" | hexdump -Cv
}
