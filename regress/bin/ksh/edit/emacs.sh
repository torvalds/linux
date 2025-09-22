#!/bin/sh
#
# $OpenBSD: emacs.sh,v 1.15 2021/09/02 07:14:15 jasper Exp $
#
# Copyright (c) 2017 Anton Lindqvist <anton@openbsd.org>
# Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
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

. "${1:-.}/subr.sh"

KSH=$2

EDITOR=
ENV=
HISTFILE=
MAIL=
PS1=' # '
VISUAL=emacs
export EDITOR ENV HISTFILE MAIL PS1 VISUAL

# The function testseq() sets up a pseudo terminal and feeds its first
# argument to a shell on standard input.  It then checks that output
# from the shell to the pseudo terminal agrees with the second argument.

# auto-insert
testseq "abc" " # abc"

# insertion of valid UTF-8
testseq "z\0002\0302\0200" " # z\b\0302\0200z\b"
testseq "z\0002\0337\0277" " # z\b\0337\0277z\b"
testseq "z\0002\0340\0240\0200" " # z\b\0340\0240\0200z\b"
testseq "z\0002\0354\0277\0277" " # z\b\0354\0277\0277z\b"
testseq "z\0002\0355\0200\0200" " # z\b\0355\0200\0200z\b"
testseq "z\0002\0355\0237\0277" " # z\b\0355\0237\0277z\b"
testseq "z\0002\0356\0200\0200" " # z\b\0356\0200\0200z\b"
testseq "z\0002\0357\0277\0277" " # z\b\0357\0277\0277z\b"
testseq "z\0002\0360\0220\0200\0200" " # z\b\0360\0220\0200\0200z\b"
testseq "z\0002\0360\0277\0277\0277" " # z\b\0360\0277\0277\0277z\b"
testseq "z\0002\0361\0200\0200\0200" " # z\b\0361\0200\0200\0200z\b"
testseq "z\0002\0363\0277\0277\0277" " # z\b\0363\0277\0277\0277z\b"
testseq "z\0002\0364\0200\0200\0200" " # z\b\0364\0200\0200\0200z\b"
testseq "z\0002\0364\0217\0277\0277" " # z\b\0364\0217\0277\0277z\b"

# insertion of incomplete UTF-8
testseq "z\0002\0302\0006" " # z\b\0302z\bz"
testseq "z\0002\0377\0006" " # z\b\0377z\bz"
testseq "z\0002\0337\0006" " # z\b\0337z\bz"
testseq "z\0002\0340\0006" " # z\b\0340z\bz"
testseq "z\0002\0357\0006" " # z\b\0357z\bz"
testseq "z\0002\0364\0006" " # z\b\0364z\bz"
testseq "z\0002\0340\0240\0006" " # z\b\0340\0240z\bz"
testseq "z\0002\0354\0277\0006" " # z\b\0354\0277z\bz"
testseq "z\0002\0355\0200\0006" " # z\b\0355\0200z\bz"
testseq "z\0002\0355\0237\0006" " # z\b\0355\0237z\bz"
testseq "z\0002\0356\0200\0006" " # z\b\0356\0200z\bz"
testseq "z\0002\0357\0277\0006" " # z\b\0357\0277z\bz"
testseq "z\0002\0360\0220\0200\0006" " # z\b\0360\0220\0200z\bz"
testseq "z\0002\0360\0277\0277\0006" " # z\b\0360\0277\0277z\bz"
testseq "z\0002\0361\0200\0200\0006" " # z\b\0361\0200\0200z\bz"
testseq "z\0002\0363\0200\0200\0006" " # z\b\0363\0200\0200z\bz"
testseq "z\0002\0363\0277\0277\0006" " # z\b\0363\0277\0277z\bz"
testseq "z\0002\0364\0200\0200\0006" " # z\b\0364\0200\0200z\bz"
testseq "z\0002\0364\0217\0277\0006" " # z\b\0364\0217\0277z\bz"

# insertion of invalid bytes
testseq "z\0002\0300\0277" " # z\b\0300z\b\b\0300\0277z\b"
testseq "z\0002\0301\0277" " # z\b\0301z\b\b\0301\0277z\b"
testseq "z\0002\0360\0217" " # z\b\0360z\b\b\0360\0217z\b"
testseq "z\0002\0365\0217" " # z\b\0365z\b\b\0365\0217z\b"
testseq "z\0002\0367\0217" " # z\b\0367z\b\b\0367\0217z\b"
testseq "z\0002\0370\0217" " # z\b\0370z\b\b\0370\0217z\b"
testseq "z\0002\0377\0217" " # z\b\0377z\b\b\0377\0217z\b"

# insertion of excessively long encodings
testseq "z\0002\0340\0200\0200" \
	" # z\b\0340z\b\b\0340\0200z\b\b\0340\0200\0200z\b"
testseq "z\0002\0340\0201\0277" \
	" # z\b\0340z\b\b\0340\0201z\b\b\0340\0201\0277z\b"
testseq "z\0002\0340\0202\0200" \
	" # z\b\0340z\b\b\0340\0202z\b\b\0340\0202\0200z\b"
testseq "z\0002\0340\0237\0277" \
	" # z\b\0340z\b\b\0340\0237z\b\b\0340\0237\0277z\b"
testseq "z\0002\0360\0200\0200\0200" \
  " # z\b\0360z\b\b\0360\0200z\b\b\0360\0200\0200z\b\b\0360\0200\0200\0200z\b"
testseq "z\0002\0360\0217\0277\0277" \
  " # z\b\0360z\b\b\0360\0217z\b\b\0360\0217\0277z\b\b\0360\0217\0277\0277z\b"

# insertion of surrogates and execessive code points
testseq "z\0002\0355\0240\0200" \
	" # z\b\0355z\b\b\0355\0240z\b\b\0355\0240\0200z\b"
testseq "z\0002\0355\0277\0277" \
	" # z\b\0355z\b\b\0355\0277z\b\b\0355\0277\0277z\b"
testseq "z\0002\0364\0220\0200\0200" \
  " # z\b\0364z\b\b\0364\0220z\b\b\0364\0220\0200z\b\b\0364\0220\0200\0200z\b"
testseq "z\0002\0364\0277\0277\0277" \
  " # z\b\0364z\b\b\0364\0277z\b\b\0364\0277\0277z\b\b\0364\0277\0277\0277z\b"

# insertion of unmatched meta sequence
testseq "z\0002\0033[3z" " # z\b\0007"

# ^C, ^G: abort
testseq "echo 1\0003" " # echo 1\r\n # "
testseq "echo 1\0007" " # echo 1\r\n # "

# ^B, Left: backward-char
testseq "\0002" " # \0007"
testseq "a\0002" " # a\0010"
testseq "\0303\0266\0002" " # \0303\0266\0010"
testseq "a\0033[D" " # a\0010"
testseq "a\0033OD" " # a\0010"

# ^[b, Ctrl-Left: backward-word
testseq "\0033b" " # \0007"
testseq "a1_$\0033b" " # a1_$\0010\0010\0010\0010"
testseq "a1 \0303\0266\0033b\0033b" " # a1 \0303\0266\0010\0010\0010\0010"
testseq "a1_$\0033[1;5D" " # a1_$\0010\0010\0010\0010"

# ^[<: beginning-of-history
testseq "\0033<" " # \0007"
testseq ": 1\n: 2\n\0033<" " # : 1\r\r\n # : 2\r\r\n # \r # : 1 \0010"

# ^A, Home, Ctrl-Down: beginning-of-line
testseq "\0001" " # "
testseq "aa\0001" " # aa\0010\0010"
testseq "\0303\0266\0001" " # \0303\0266\0010"
testseq "aa\0033[H" " # aa\0010\0010"
testseq "aa\0033OH" " # aa\0010\0010"
testseq "aa\0033[1~" " # aa\0010\0010"
testseq "aa\0033[1;5B" " # aa\0010\0010"

# [n] ^[C, ^[c: capitalize-word
testseq "\0033C\0033c" " # \0007\0007"
testseq "ab cd\0001\0033C" " # ab cd\0010\0010\0010\0010\0010Ab"
testseq "ab\0001\00332\0033C" " # ab\0010\0010Ab"
testseq "ab cd\0001\00332\0033C" " # ab cd\0010\0010\0010\0010\0010Ab Cd"
testseq "1a\0001\0033C" " # 1a\0010\00101a"
testseq "\0026\0002\0001\0033C" " # ^B\0010\0010^B"
testseq "\0303\0266b\0001\0033C" " # \0303\0266b\0010\0010\0303\0266b"

# ^[#: comment
testseq "\0033#" " # \r #  \0010\r\r\n # "
testseq "a\0033#" " # a\r # #a \0010\0010\0010\r\r\n # "

# XXX ^[^[: complete
# XXX ^X^[: complete-command
# XXX ^[^X: complete-file
# XXX ^I, ^[=: complete-list

# [n] ERASE, ^?, ^H: delete-char-backward
testseq "\0177\0010" " # \0007\0007"
testseq "ab\00332\0177" " # ab\0010\0010  \0010\0010"
testseq "a\00332\0177" " # a\0010 \0010"
testseq "\0303\0266\0177" " # \0303\0266\0010 \0010"

# [n] Delete: delete-char-forward
testseq "\0033[3~" " # \0007"
testseq "a\0033[3~" " # a\0007"
testseq "a\0001\0033[3~" " # a\0010 \0010"
testseq "ab\0001\00332\0033[3~" " # ab\0010\0010  \0010\0010"
testseq "a\0001\00332\0033[3~" " # a\0010 \0010"
testseq "\0303\0266\0001\0033[3~" " # \0303\0266\0010 \0010"

# [n] ^[ERASE, ^[^?, ^[^H, ^[h: delete-word-backward
testseq "\0033\0177\033\0010\0033h" " # \0007\0007\0007"
testseq "ab\0033\0177" " # ab\0010\0010  \0010\0010"
testseq "ab cd\00332\0033\0177" \
	" # ab cd\0010\0010\0010\0010\0010     \0010\0010\0010\0010\0010"
testseq "ab\00332\0033\0177" " # ab\0010\0010  \0010\0010"
testseq "ab \0303\0266\0033\0177" " # ab \0303\0266\0010 \0010"

# [n] ^[d: delete-word-forward
testseq "\0033d" " # \0007"
testseq "ab\0001\0033d" " # ab\0010\0010  \0010\0010"
testseq "ab cd\0001\00332\0033d" \
	" # ab cd\0010\0010\0010\0010\0010     \0010\0010\0010\0010\0010"
testseq "ab\0001\00332\0033d" " # ab\0010\0010  \0010\0010"

# [n] ^N, ^XB, Down: down-history
# [n] ^P, ^XA, Up: up-history
testseq "\0016\0030B\0020\0030A" " # \0007\0007\0007\0007"
testseq ": 1\n\0020\0016" " # : 1\r\r\n # \r # : 1 \0010\0007"
testseq ": 1\n: 2\n\0020\0020\0016" \
	" # : 1\r\r\n # : 2\r\r\n # \r # : 2 \0010\r # : 1 \0010\r # : 2 \0010"
testseq ": 1\n: 2\n\0033[A\0033[A\0033[B" \
	" # : 1\r\r\n # : 2\r\r\n # \r # : 2 \0010\r # : 1 \0010\r # : 2 \0010"
testseq ": 1\n: 2\n\0033OA\0033OA\0033OB" \
	" # : 1\r\r\n # : 2\r\r\n # \r # : 2 \0010\r # : 1 \0010\r # : 2 \0010"

# [n] ^[L, ^[l: downcase-word
testseq "\0033L\0033l" " # \0007\0007"
testseq "AB\0001\0033L" " # AB\0010\0010ab"
testseq "AB CD\0001\00332\0033L" " # AB CD\0010\0010\0010\0010\0010ab cd"
testseq "AB\0001\00332\0033L" " # AB\0010\0010ab"
testseq "1A\0001\0033L" " # 1A\0010\00101a"
testseq "\0026\0002A\0001\0033L" " # ^BA\0010\0010\0010^Ba"
testseq "\0303\0266A\0001\0033L" " # \0303\0266A\0010\0010\0303\0266a"

# ^[>: end-of-history
testseq "\0033>" " # \0007"
testseq ": 1\n\0033>" " # : 1\r\r\n # \r # : 1 \0010"

# ^E, End, Ctrl-Up: end-of-line
testseq "\0005" " # "
testseq "abc\0001\0005" " # abc\0010\0010\0010abc"
testseq "\0303\0266\0001\0005" " # \0303\0266\0010\0303\0266"
testseq "abc\0001\0033[F" " # abc\0010\0010\0010abc"
testseq "abc\0001\0033OF" " # abc\0010\0010\0010abc"
testseq "abc\0001\0033[4~" " # abc\0010\0010\0010abc"
testseq "abc\0001\0033[1;5A" " # abc\0010\0010\0010abc"

# ^_: eot
testseq "\0037" " # ^D\r\r"

# [n] ^D: eot-or-delete
testseq "\0004" " # ^D\r\r"

# ^X^X: exchange-point-and-mark
# ^[space: set-mark-command
testseq "\0030\0030" " # \0007"
testseq "abc\0033 \0001\0030\0030" " # abc\0010\0010\0010abc"
testseq "\0303\0266\0033 \0001\0030\0030" " # \0303\0266\0010\0303\0266"

# XXX ^[*: expand-file

# [n] ^F, ^XC, Right: forward-char
testseq "\0006\0030C" " # \0007\0007"
testseq "abc\0001\0006" " # abc\0010\0010\0010a"
testseq "abc\0001\00332\0006" " # abc\0010\0010\0010ab"
testseq "a\0001\00332\0006" " # a\0010a"
testseq "\0303\0266\0001\0006" " # \0303\0266\0010\0303\0266"
testseq "abc\0001\0033[C" " # abc\0010\0010\0010a"
testseq "abc\0001\0033OC" " # abc\0010\0010\0010a"

# [n] ^[f, Ctrl-Right: forward-word
testseq "\0033f" " # \0007"
testseq "ab\0001\0033f" " # ab\0010\0010ab"
testseq "ab cd\0001\00332\0033f" " # ab cd\0010\0010\0010\0010\0010ab cd"
testseq "ab\0001\00332\0033f" " # ab\0010\0010ab"
testseq "\0303\0266\0001\0033f" " # \0303\0266\0010\0303\0266"
testseq "ab\0001\0033[1;5C" " # ab\0010\0010ab"

# [n] ^[g: goto-history
testseq "\0033g" " # \0007"
testseq ": 1\n\0033g" " # : 1\r\r\n # \r # : 1 \0010"
testseq ": 1\n: 2\n\00332\0033g" " # : 1\r\r\n # : 2\r\r\n # \r # : 2 \0010"
testseq ": 1\n\00332\0033g" " # : 1\r\r\n # \0007"

# KILL: kill-line
testseq "\0025" " # \r #  \0010"
testseq "ab\0025" " # ab\r #    \0010\0010\0010"

# [n] ^K: kill-to-eol
testseq "\0013" " # "
testseq "abc\0002\0002\0013" " # abc\0010\0010  \0010\0010"

# XXX ^[?: list
# XXX ^X?: list-command
# XXX ^X^Y: list-file

# ^J, ^M: newline
testseq "\0012\0015" " # \r\r\n # \r\r\n # "
testseq ": 1\0012" " # : 1\r\r\n # "
testseq ": 1\0001\0012" " # : 1\0010\0010\0010\r\r\n # "

# ^O: newline-and-next
testseq "\0017" " # \r\r\n # \0007"
testseq ": 1\n: 2\n\0020\0020\0017" \
	" # : 1\r\r\n # : 2\r\r\n # \r # : 2 \0010\r # : 1 \0010\r\r\n # \r # : 2 \0010"

# QUIT: no-op
testseq "\0034" " # "

# [n] ^[., ^[_: prev-hist-word
testseq "\0033.\0033_" " # \0007\0007"
testseq ": 1\n\0033." " # : 1\r\r\n # 1"
testseq ": 1\n\00331\0033." " # : 1\r\r\n # :"
testseq ": 1\n\00333\0033." " # : 1\r\r\n # "

# ^V, ^^: quote
testseq "\0026\0001" " # ^A"
testseq "\0036\0001" " # ^A"

# [n] ^[^]: search-character-backward
testseq "\0033\0035a" " # \0007"
testseq "echo\0033\0035e" " # echo\0010\0010\0010\0010"
testseq "echo\0033\0035a" " # echo\0007"
testseq "eecho\00332\0033\0035e" " # eecho\0010\0010\0010\0010\0010"
testseq "echo\00332\0033\0035e" " # echo\0007"

# [n] ^]: search-character-forward
testseq "\0035a" " # \0007"
testseq "echo\0001\0035o" " # echo\0010\0010\0010\0010ech"
testseq "echo\0001\0035a" " # echo\0010\0010\0010\0010\0007"
testseq "echoo\0001\00332\0035o" " # echoo\0010\0010\0010\0010\0010echo"
# XXX differs from search-character-backward, should ring bell
testseq "echo\0001\00332\0035o" " # echo\0010\0010\0010\0010ech"

# ^R: search-history
testseq "\0022" " # \r\nI-search: "
testseq "echo\n\0022e" \
	" # echo\r\r\n\r\n # \r\nI-search: \r\n\r # echo \0010\0010\0010\0010"
testseq "echo\n\0022a" " # echo\r\r\n\r\n # \r\nI-search: \0007\r\nI-search: a"

# ^T: transpose-chars
testseq "\0024" " # \0007"
testseq "a\0024" " # a\0007"
testseq "ab\0024" " # ab\0010\0010ba"
testseq "ab\0001\0024" " # ab\0010\0010\0007"
# XXX UTF-8 testseq "\0303\0266a\0024" " # \0303\0266a\0010\0010a\303\0266"

# [n] ^[U, ^[u: upcase-word
testseq "\0033U\0033u" " # \0007\0007"
testseq "ab\0001\0033U" " # ab\0010\0010AB"
testseq "ab cd\0001\00332\0033U" " # ab cd\0010\0010\0010\0010\0010AB CD"
testseq "ab\0001\00332\0033U" " # ab\0010\0010AB"
testseq "1a\0001\0033U" " # 1a\0010\00101A"
testseq "\0026\0002a\0001\0033U" " # ^Ba\0010\0010\0010^BA"
testseq "\0303\0266a\0001\0033U" " # \0303\0266a\0010\0010\0303\0266A"

# ^Y: yank
testseq "\0031" " # \r\nnothing to yank\r\n # "
testseq "abc\0027\0031" " # abc\0010\0010\0010   \0010\0010\0010abc"
testseq "ab/cd\0027\0001\0031" \
	" # ab/cd\0010\0010  \0010\0010\0010\0010\0010cdab/\0010\0010\0010"

# ^[y: yank-pop
testseq "\0033y" " # \r\nyank something first\r\n # "
testseq "ab/cd\0027\0027\0031\0033y" \
	" # ab/cd\0010\0010  \0010\0010\0010\0010\0010   \0010\0010\0010ab/\0010\0010\0010   \0010\0010\0010cd"
