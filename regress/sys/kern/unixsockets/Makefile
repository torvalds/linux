# $OpenBSD: Makefile,v 1.10 2021/12/15 22:29:55 mvs Exp $

# Copyright (c) 2021 Makkoveev Vitaliy <mvs@openbsd.org>
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

WARNINGS = yes

PROGS = undgram_selfconn ungc unixsock_test

PROGS += unconacc
LDADD_unconacc = -lpthread

PROGS += undgram_conclose
LDADD_undgram_conclose = -lpthread

PROGS += unfdpassfail
LDADD_unfdpassfail = -lpthread

PROGS += unsendrecvthr
LDADD_unsendrecvthr = -lpthread

PROGS += unsopassgc
LDADD_unsopassgc = -lpthread

CLEANFILES += *.socket

run-regress-undgram_conclose:
	ulimit -n 256; ./undgram_conclose

run-regress-unsopassgc:
	ulimit -n 256; ./unsopassgc

.include <bsd.regress.mk>
