#	$OpenBSD: Makefile,v 1.2 2020/12/17 00:51:12 bluhm Exp $

# Copyright (c) 2019 Moritz Buhl <openbsd@moritzbuhl.de>
# Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
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

# Each test program in PROGS may define several numbered subtests.
# In a first step compile all programs and extract their parameters.
# For each PROG define new regression subtests based on the test number.

.if defined(NUMBERS)
REGRESS_TARGETS =	${NUMBERS:S/^/run-${PROG}-/}
.else
REGRESS_TARGETS =	${PROGS:S/^/run-/}
.endif

PROGS =		ptrace_test

. for p in ${PROGS}
SRCS_$p =		$p.c atf-c.c
. endfor

LDADD =	-lpthread

.for p in ${PROGS}
run-$p: $p
	ntests="`./$p -n`" && \
	echo "1..$$ntests" && \
	tnumbers="`jot -ns' ' - 1 $$ntests`" && \
	${.MAKE} -C ${.CURDIR} PROG=$p NUMBERS="$$tnumbers" regress
.endfor

.if defined(NUMBERS)
CUR_USER !=		id -g

. for n in ${NUMBERS}
DESCR_$n !=		eval `./${PROG} -i $n` && echo $$DESCR
REQ_USER_$n !=		eval `./${PROG} -i $n` && echo $$REQ_USER

.  if ${REQ_USER_$n} == "root"
REGRESS_ROOT_TARGETS +=	run-${PROG}-$n
.  endif

run-${PROG}-$n:
	@echo "$n ${DESCR_$n}"
.  if ${REQ_USER_$n} == "root"
	${SUDO} ./${PROG} -r $n
.  elif ${REQ_USER_$n} == "unprivileged" && ${CUR_USER} == 0
	${SUDO} su ${BUILDUSER} -c exec ./${PROG} -r $n
.  elif ${REQ_USER_$n} == "unprivileged" || ${REQ_USER_$n} == ""
	./${PROG} -r $n
.  else
	# bad REQ_USER: ${REQ_USER_$n}
	false
.  endif

. endfor
.endif

.include <bsd.regress.mk>
