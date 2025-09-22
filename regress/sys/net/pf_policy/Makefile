#	$OpenBSD: Makefile,v 1.4 2024/02/20 21:10:26 bluhm Exp $

# Copyright (c) 2022 Alexandr Nedvedicky <sashan@openbsd.org>
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
#

#
# REMOTE_ADDR use any remote host which is
# reachable for ping(8)
#
REMOTE_ADDR ?= 10.188.210.50

TESTS_PASS =	absolute	\
		quick		\
		relative

TESTS_BLOCK =	list		\
		loop-relative	\
		loop		\
		nesting		\
		simple

TESTS_ONCE =	nesting-once	\
		once

.include <bsd.own.mk>

.if ! (make(clean) || make(cleandir) || make(obj))

REACHABLE != ping -c 1 -w 1 ${REMOTE_ADDR} > /dev/null 2>&1 && echo yes || :
.if empty(REACHABLE)
regress:
	@echo Cannot reach ${REMOTE_ADDR}
	@echo SKIPPED
.endif

.endif

REGRESS_SETUP_ONCE = enable-pf
enable-pf:
	${SUDO} pfctl -e || true

REGRESS_TARGETS += check-probe-host

check-probe-host:
	ping -c 1 -w 1 ${REMOTE_ADDR}

.for rules in ${TESTS_PASS}
REGRESS_TARGETS +=	run-pass-${rules}
run-pass-${rules}:
	${SUDO} pfctl -a "regress/*" -Fa
	${SUDO} pfctl -a "regress" -f ${.CURDIR}/${rules}.conf
	ping -c 1 -w 1 ${REMOTE_ADDR}
.endfor

.for rules in ${TESTS_BLOCK}
REGRESS_TARGETS +=	run-block-${rules}
run-block-${rules}:
	${SUDO} pfctl -a "regress/*" -Fa
	ping -c 1 -w 1 ${REMOTE_ADDR}
	${SUDO} pfctl -a "regress" -f ${.CURDIR}/${rules}.conf
	ping -c 1 -w 1 ${REMOTE_ADDR} || true
.endfor

.for rules in ${TESTS_ONCE}
REGRESS_TARGETS +=	run-once-${rules}
run-once-${rules}:
	${SUDO} pfctl -a "regress/*" -Fa
	${SUDO} pfctl -a "regress" -f ${.CURDIR}/${rules}.conf
	ping -c 1 -w 1 ${REMOTE_ADDR}
	ping -c 1 -w 1 ${REMOTE_ADDR} || true
.endfor

REGRESS_CLEANUP += cleanup

cleanup:
	${SUDO} pfctl -a "regress/*" -Fr

.include <bsd.regress.mk>
