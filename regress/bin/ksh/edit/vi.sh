#!/bin/sh
#
# $OpenBSD: vi.sh,v 1.13 2025/05/19 14:36:03 schwarze Exp $
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

. "${1:-.}/subr.sh"

KSH=$2

EDITOR=
ENV=
HISTFILE=
MAIL=
PS1=' # '
VISUAL=vi
export EDITOR ENV HISTFILE MAIL PS1 VISUAL

# The function testseq() sets up a pseudo terminal and feeds its first
# argument to a shell on standard input.  It then checks that output
# from the shell to the pseudo terminal agrees with the second argument.

# ^H, ^?: Erase.
testseq "ab\bc" " # ab\b \bc"
testseq "ab\0177c" " # ab\b \bc"

# ^J, ^M: End of line.
testseq "echo a\nb" " # echo a\r\r\na\r\n # b"
testseq "echo a\rb" " # echo a\r\r\na\r\n # b"
testseq "echo a\0033\nb" " # echo a\b\r\r\na\r\n # b"
testseq "echo a\0033\rb" " # echo a\b\r\r\na\r\n # b"

# ^U: Kill.
testseq "ab\0033ic\0025d" " # ab\bcb\b\b\bb  \b\b\bdb\b"

# ^V: Literal next.
testseq "a\0026\0033b" " # a^\b^[b"

# ^W: Word erase.
testseq "one two\0027rep" " # one two\b\b\b   \b\b\brep"

# A: Append at end of line.
# 0: Move to column 0.
testseq "one\00330A two" " # one\b\b\bone two"
testseq "one\003302A two\0033" " # one\b\b\bone two two\b"
testseq "\0200abcdef\00330lrxll" " # \0200abcdef\b\r # \0200x\bxb"
testseq "\0270\0202A\00330i\0340\0033" " # \0270\0202A\b\b\0340\0270\0202A\b\b"
testseq "\0200abcdef\00330ix\0033" \
	" # \0200abcdef\b\r # x\0200abcdef\r # x\0200\b"

# a: Append.
# .: Redo.
testseq "ab\00330axy" " # ab\b\baxb\byb\b"
testseq "ab\003302axy\0033" " # ab\b\baxb\byb\bxyb\b\b"
testseq "ab\00330axy\0033." " # ab\b\baxb\byb\b\byxyb\b\b"

# B: Move back big word.
testseq "one 2.0\0033BD" " # one 2.0\b\b\b   \b\b\b\b"

# b: Move back word.
# C: Change to end of line.
# D: Delete to end of line.
testseq "one ab.cd\0033bDa.\00332bD" \
	" # one ab.cd\b\b  \b\b\b..\b\b\b\b    \b\b\b\b\b"
testseq "one two\0033bCrep" " # one two\b\b\b   \b\b\brep"
testseq "\0302\0251\0303\0200a\0033DP" \
	" # \0302\0251\0303\0200a\b \b\ba\0303\0200\b\b"

# c: Change region.
testseq "one two\0033cbrep" " # one two\b\b\bo  \b\b\bro\beo\bpo\b"
testseq "one two\00332chx" " # one two\b\b\bo  \b\b\bxo\b"

# d: Delete region.
testseq "one two\0033db" " # one two\b\b\bo  \b\b\b"
testseq "one two xy\00332db" " # one two xy\b\b\b\b\b\by     \b\b\b\b\b\b"

# E: Move to end of big word.
testseq "1.00 two\00330ED" " # 1.00 two\b\r # 1.0     \b\b\b\b\b\b"

# e: Move to end of word.
testseq "onex two\00330eD" " # onex two\b\r # one     \b\b\b\b\b\b"

# F: Find character backward.
# ;: Repeat last search.
# ,: Repeat last search in opposite direction.
testseq "hello\00332FlD" " # hello\b\b\b   \b\b\b\b"
testseq "hello\0033Flix\0033;ix" " # hello\b\bxlo\b\b\b\bxlxlo\b\b\b\b"
testseq "hello\00332Flix\00332,ix" " # hello\b\b\bxllo\b\b\b\bxlxlo\b\b"

# f: Find character forward.
testseq "hello\003302flD" " # hello\b\b\b\b\bhel  \b\b\b"

# h, ^H: Move left.
# i: Insert.
testseq "hello\00332hix" " # hello\b\b\bxllo\b\b\b"
testseq "hello\00332\b2ix\0033" " # hello\b\b\bxllo\b\b\bxllo\b\b\b\b"

# I: Insert before first non-blank.
# ^: Move to the first non-whitespace character.
testseq "  ab\0033Ixy" " #   ab\b\bxab\b\byab\b\b"
testseq "  ab\00332Ixy\0033" " #   ab\b\bxab\b\byab\b\bxyab\b\b\b"
testseq "  ab\0033^ixy" " #   ab\b\bxab\b\byab\b\b"

# L: Undefined command (beep).
testseq "ab\0033Lx" " # ab\b\a \b\b"

# l, space: Move right.
# ~: Change case.
testseq "abc\003302l~" " # abc\b\b\babC\b"
testseq "abc\00330 rx" " # abc\b\b\bax\b"

# P: Paste at current position.
testseq "abcde\0033hDhP" " # abcde\b\b  \b\b\b\bdebc\b\b\b"
testseq "abcde\0033hDh2P" " # abcde\b\b  \b\b\b\bdedebc\b\b\b"
testseq "A\0033xa\0303\0200\00332Px" \
	" # A\b \b\0303\0200\bAA\0303\0200\b\b\0303\0200 \b\b"
testseq "\0302\0251\0033xaA\0033Px" \
	" # \0302\0251\b  \b\bA\b\0302\0251A\b\bA  \b\b\b"
testseq "\0302\0251\0033xa\0303\0200\0033Px" \
 " # \0302\0251\b  \b\b\0303\0200\b\0302\0251\0303\0200\b\b\0303\0200  \b\b\b"

# p: Paste after current position.
testseq "abcd\0033hDhp" " # abcd\b\b  \b\b\b\bacdb\b\b"
testseq "abcd\0033hDh2p" " # abcd\b\b  \b\b\b\bacdcdb\b\b"
testseq "A\0033xa\0303\0200\0033px" " # A\b \b\0303\0200\b\0303\0200A\b \b\b"
testseq "\0302\0251\0033xaA\0033px" \
	" # \0302\0251\b  \b\bA\bA\0302\0251\b  \b\b\b" 
testseq "\0302\0251\0033xa\0303\0200\0033px" \
	" # \0302\0251\b  \b\b\0303\0200\b\0303\0200\0302\0251\b  \b\b\b"

# R: Replace.
testseq "abcd\00332h2Rx\0033" " # abcd\b\b\bxx\b"
testseq "abcdef\00334h2Rxy\0033" " # abcdef\b\b\b\b\bxyxy\b"

# r: Replace character.
testseq "abcd\00332h2rxiy" " # abcd\b\b\bxx\byxd\b\b"
testseq "\0303\0266\0033ro" " # \0303\0266\bo \b\b"
testseq "\0342\0202\0254\0033ro" " # \0342\0202\0254\bo  \b\b\b"
testseq "\0303\0266\00332ro" " # \0303\0266\b\a"

# S: Substitute whole line.
testseq "oldst\0033Snew" " # oldst\b\b\b\b\b     \r # new"
testseq "oldstr\033Snew" " # oldstr\b\r #       \r # new"

# s: Substitute.
testseq "abcd\00332h2sx" " # abcd\b\b\bd  \b\b\bxd\b"
testseq "\0303\0266\0033s" " # \0303\0266\b  \b\b"

# T: Move backward after character.
testseq "helloo\0033TlD" " # helloo\b\b  \b\b\b"
testseq "hello\00332TlD" " # hello\b\b  \b\b\b"

# t: Move forward before character.
testseq "abc\00330tcD" " # abc\b\b\ba  \b\b\b"
testseq "hello\003302tlD" " # hello\b\b\b\b\bhe   \b\b\b\b"

# U: Undo all changes.
testseq "test\0033U" " # test\b\b\b\b    \b\b\b\b"

# u: Undo.
testseq "test\0033hxu" " # test\b\bt \b\bst\b\b"

# W: Move forward big word.
testseq "1.0 two\00330WD" " # 1.0 two\b\r # 1.0    \b\b\b\b"

# w: Move forward word.
testseq "ab cd ef\003302wD" " # ab cd ef\b\r # ab cd   \b\b\b"

# X: Delete previous character.
testseq "abcd\00332X" " # abcd\b\b\bd  \b\b\b"

# x: Delete character.
# |: Move to column.
testseq "abcd\00332|2x" " # abcd\b\b\bd  \b\b\b"
testseq "\0303\0266a\0033xx" " # \0303\0266a\b \b\b  \b\b"

# Y: Yank to end of line.
testseq "abcd\0033hYp" " # abcd\b\bccdd\b\b"

# y: Yank region.
# $: Move to the last character.
testseq "abcd\00332h2ylp" " # abcd\b\b\bbbccd\b\b\b"
testseq "abcd\00332h2yl\$p" " # abcd\b\b\bbcdbc\b"

# %: Find match.
testseq "(x)\0033%lrc" " # (x)\b\b\b(c\b"
testseq "(x)\00330%hrc" " # (x)\b\b\b(x\bc\b"

# ^R: Redraw.
testseq "test\0033h\0022" " # test\b\b\r\r\n # test\b\b"
