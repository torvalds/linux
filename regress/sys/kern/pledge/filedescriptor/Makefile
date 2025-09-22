#	$OpenBSD: Makefile,v 1.2 2018/04/11 16:47:17 bluhm Exp $

# Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
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

# Check that a file descriptor's plegde flag is correctly propagated.
# The test covers the system calls dup, dup2, dup3, open /dev/fd, and
# file descriptor passing.  The fstat(1) output is analysed.

PROGS =		dup dup2 dup3 dev pass
WARNINGS =	yes
CLEANFILES =	*.fstat

.if make (regress) || make (all)
.BEGIN:
	rm -f -- *.fstat
.endif

.for p in ${PROGS}

SRCS_$p =			main.c $p.c
REGRESS_TARGETS +=	run-regress-$p

$p.fstat: $p
	./$p >$@.tmp
	mv $@.tmp $@
	# file descriptor 3 has been opened before pledge
	grep ' 3 /.* r ' $@
	# file descriptor 4 has been opened after pledge
	grep ' 4 /.* rp ' $@

.endfor

run-regress-dup: dup.fstat
	# file descriptor 5 has been dup(2)ed from pre-pledge
	grep ' 5 /.* r ' dup.fstat
	# file descriptor 6 has been dup(2)ed from post-pledge
	grep ' 6 /.* rp ' dup.fstat

run-regress-dup2: dup2.fstat
	# file descriptor 5 has been dup2(2)ed from pre-pledge
	grep ' 5 /.* r ' dup2.fstat
	# file descriptor 6 has been dup2(2)ed from post-pledge
	grep ' 6 /.* rp ' dup2.fstat
	# dup2(2) closes pre-pledge and overwrites with post-pledge
	grep ' 7 /.* rp ' dup2.fstat
	# dup2(2) closes post-pledge and overwrites with pre-pledge
	grep ' 8 /.* r ' dup2.fstat
	# dup2(2) pre-pledge onto itself
	grep ' 9 /.* r ' dup2.fstat
	# dup2(2) post-pledge onto itself
	grep ' 10 /.* rp ' dup2.fstat

run-regress-dup3: dup3.fstat
	# file descriptor 5 has been dup3(2)ed from pre-pledge
	grep ' 5 /.* re ' dup3.fstat
	# file descriptor 6 has been dup3(2)ed from post-pledge
	grep ' 6 /.* rep ' dup3.fstat
	# dup3(2) closes pre-pledge and overwrites with post-pledge
	grep ' 7 /.* rep ' dup3.fstat
	# dup3(2) closes post-pledge and overwrites with pre-pledge
	grep ' 8 /.* re ' dup3.fstat
	# dup3(2) pre-pledge onto itself fails
	grep ' 9 /.* r ' dup3.fstat
	# dup3(2) post-pledge onto itself fails
	grep ' 10 /.* rp ' dup3.fstat

run-regress-dev: dev.fstat
	# file descriptor 5 has been open(2)ed from pre-pledge /dev/fd/3
	grep ' 5 /.* r ' dev.fstat
	# file descriptor 6 has been open(2)ed from post-pledge /dev/fd/4
	grep ' 6 /.* rp ' dev.fstat

run-regress-pass: pass.fstat
	# file descriptor 7 has been passed from pre-pledge
	grep ' 7 /.* r ' pass.fstat
	# file descriptor 8 has been passed from post-pledge
	grep ' 8 /.* rp ' pass.fstat

.include <bsd.regress.mk>
