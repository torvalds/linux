#!/bin/sh
#
# $OpenBSD: getopt.sh,v 1.2 2020/05/27 22:32:22 schwarze Exp $
#
# Copyright (c) 2020 Ingo Schwarze <schwarze@openbsd.org>
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

# Run ./getopt-test once.
# Function arguments:
# 1. optstring argument for getopt(3)
# 2. space-separated command line arguments
# 3. expected output from ./getopt-test
test1_getopt()
{
	result=$(OPTS=$1 ./getopt-test $2)
	test "$result" == "$3" && return
	echo "OPTS=$1 ./getopt-test $2"
	echo "expected: $3"
	echo "result:   $result"
	irc=1
}

# Test both without and with the optstring modifier "+",
# verifying that it makes no difference.
test2_getopt()
{
	test1_getopt "$1" "$2" "$3"
	test1_getopt "+$1" "$2" "$3"
}

# Also test with the GNU "-" optstring modifier,
# veryfying that it only changes ARG() to NONE().
# This test function is inadequate in two situations:
# a) options follow non-option arguments that terminate option processing
# b) or any arguments follow explicit "--".
# In these cases, use test2_getopt() plus a separate test1_getopt(-...).
test3_getopt()
{
	test2_getopt "$1" "$2" "$3"
	test1_getopt "-$1" "$2" $(echo $3 | sed s/ARG/NONE/g)
}

irc=0

# valid isolated options without arguments
test3_getopt ax '-a -x arg' 'OPT(a)OPT(x)ARG(arg)'
test3_getopt x- '- -x arg' 'OPT(-)OPT(x)ARG(arg)'

# invalid isolated options without arguments
test3_getopt ax '-a -y arg' 'OPT(a)ERR(?y)ARG(arg)'
test1_getopt :ax '-a -y arg' 'OPT(a)ERR(?y)ARG(arg)'
test2_getopt x '- -x arg' 'ARG(-)ARG(-x)ARG(arg)'
test1_getopt -x '- -x arg' 'NONE(-)OPT(x)NONE(arg)'
test3_getopt a- '-a - -x arg' 'OPT(a)OPT(-)ERR(?x)ARG(arg)'
test1_getopt :a- '-a - -x arg' 'OPT(a)OPT(-)ERR(?x)ARG(arg)'

# valid grouped options without arguments
test3_getopt ax '-ax arg' 'OPT(a)OPT(x)ARG(arg)'
test3_getopt ax- '-a- -x arg' 'OPT(a)OPT(-)OPT(x)ARG(arg)'
test3_getopt abx- '-a-b -x arg' 'OPT(a)OPT(-)OPT(b)OPT(x)ARG(arg)'
test3_getopt ax- '--a -x arg' 'OPT(-)OPT(a)OPT(x)ARG(arg)'

# invalid grouped options without arguments
test3_getopt ax '-ay arg' 'OPT(a)ERR(?y)ARG(arg)'
test1_getopt :ax '-ay arg' 'OPT(a)ERR(?y)ARG(arg)'
test3_getopt ax '-a- -x arg' 'OPT(a)ERR(?-)OPT(x)ARG(arg)'
test1_getopt :ax '-a- -x arg' 'OPT(a)ERR(?-)OPT(x)ARG(arg)'
test3_getopt abx '-a-b -x arg' 'OPT(a)ERR(?-)OPT(b)OPT(x)ARG(arg)'
test1_getopt :abx '-a-b -x arg' 'OPT(a)ERR(?-)OPT(b)OPT(x)ARG(arg)'
test3_getopt ax '--a -x arg' 'ERR(?-)OPT(a)OPT(x)ARG(arg)'
test1_getopt :ax '--a -x arg' 'ERR(?-)OPT(a)OPT(x)ARG(arg)'

# non-option arguments terminating option processing
test2_getopt ax '-a arg -x' 'OPT(a)ARG(arg)ARG(-x)'
test1_getopt -ax '-a arg1 -x arg2' 'OPT(a)NONE(arg1)OPT(x)NONE(arg2)'
test2_getopt ax '-a -- -x' 'OPT(a)ARG(-x)'
test1_getopt -ax '-a -- -x' 'OPT(a)ARG(-x)'
test2_getopt ax '-a - -x' 'OPT(a)ARG(-)ARG(-x)'
test1_getopt -ax '-a - -x arg' 'OPT(a)NONE(-)OPT(x)NONE(arg)'

# the ':' option never works
test1_getopt ::a '-:a arg' 'ERR(?:)OPT(a)ARG(arg)'
test1_getopt :::a '-: arg -a' 'ERR(?:)ARG(arg)ARG(-a)'

# isolated options with arguments
test3_getopt o: '-o' 'ERR(?o)'
test1_getopt :o: '-o' 'ERR(:o)'
test3_getopt o-: '-' 'ERR(?-)'
test1_getopt :-: '-' 'ERR(:-)'
test3_getopt o:x '-o arg -x arg' 'OPT(oarg)OPT(x)ARG(arg)'
test3_getopt o:x '-oarg -x arg' 'OPT(oarg)OPT(x)ARG(arg)'
test3_getopt o::x '-oarg -x arg' 'OPT(oarg)OPT(x)ARG(arg)'
test2_getopt o::x '-o arg -x' 'OPT(o)ARG(arg)ARG(-x)'
test1_getopt -o::x '-o arg1 -x arg2' 'OPT(o)NONE(arg1)OPT(x)NONE(arg2)'
test3_getopt o:x '-o -x arg' 'OPT(o-x)ARG(arg)'
test3_getopt o:x '-o -- -x arg' 'OPT(o--)OPT(x)ARG(arg)'
test3_getopt x-: '- arg -x arg' 'OPT(-arg)OPT(x)ARG(arg)'
test3_getopt x-: '--arg -x arg' 'OPT(-arg)OPT(x)ARG(arg)'
test3_getopt x-:: '--arg -x arg' 'OPT(-arg)OPT(x)ARG(arg)'
test2_getopt x-:: '- arg -x' 'OPT(-)ARG(arg)ARG(-x)'
test1_getopt --::x '- arg1 -x arg2' 'OPT(-)NONE(arg1)OPT(x)NONE(arg2)'
test3_getopt x-: '- -x arg' 'OPT(--x)ARG(arg)'
test3_getopt x-: '- -- -x arg' 'OPT(---)OPT(x)ARG(arg)'

# grouped options with arguments
test3_getopt ao: '-ao' 'OPT(a)ERR(?o)'
test1_getopt :ao: '-ao' 'OPT(a)ERR(:o)'
test3_getopt a-: '-a-' 'OPT(a)ERR(?-)'
test1_getopt :a-: '-a-' 'OPT(a)ERR(:-)'
test3_getopt ao:x '-ao arg -x arg' 'OPT(a)OPT(oarg)OPT(x)ARG(arg)'
test3_getopt ao:x '-aoarg -x arg' 'OPT(a)OPT(oarg)OPT(x)ARG(arg)'
test3_getopt ao::x '-aoarg -x arg' 'OPT(a)OPT(oarg)OPT(x)ARG(arg)'
test2_getopt ao::x '-ao arg -x' 'OPT(a)OPT(o)ARG(arg)ARG(-x)'
test1_getopt -ao::x '-ao arg1 -x arg2' 'OPT(a)OPT(o)NONE(arg1)OPT(x)NONE(arg2)'
test3_getopt ao:x '-ao -x arg' 'OPT(a)OPT(o-x)ARG(arg)'
test3_getopt ao:x '-ao -- -x arg' 'OPT(a)OPT(o--)OPT(x)ARG(arg)'
test3_getopt a-:x '-a- arg -x arg' 'OPT(a)OPT(-arg)OPT(x)ARG(arg)'
test3_getopt a-:x '-a-arg -x arg' 'OPT(a)OPT(-arg)OPT(x)ARG(arg)'
test3_getopt a-::x '-a-arg -x arg' 'OPT(a)OPT(-arg)OPT(x)ARG(arg)'
test2_getopt a-::x '-a- arg -x' 'OPT(a)OPT(-)ARG(arg)ARG(-x)'
test1_getopt -a-::x '-a- arg1 -x arg2' 'OPT(a)OPT(-)NONE(arg1)OPT(x)NONE(arg2)'
test3_getopt a-:x '-a- -x arg' 'OPT(a)OPT(--x)ARG(arg)'
test3_getopt a-:x '-a- -- -x arg' 'OPT(a)OPT(---)OPT(x)ARG(arg)'

exit $irc
