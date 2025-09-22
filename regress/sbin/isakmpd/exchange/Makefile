#	$OpenBSD: Makefile,v 1.2 2017/07/03 18:19:55 bluhm Exp $
#	$EOM: Makefile,v 1.8 2000/03/28 21:22:07 ho Exp $

#
# Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# This code was written under funding by Ericsson Radio Systems.
#

RUN=		${SUDO} env ISAKMPD=${TOPOBJ}/isakmpd sh ${.CURDIR}/run.sh

TESTS=		def

REGRESS_TARGETS=${TESTS:S/^/test-/}

.for TEST in ${TESTS}
test-${TEST}:
	@echo Testing "${TEST}" test as initiator
	@${RUN} ${RUNFLAGS} ${.CURDIR}/${TEST}
#	@echo Testing "${TEST}" test as responder
#	@${RUN} -r ${RUNFLAGS} ${.CURDIR}/${TEST}
.endfor

.PHONY: ${REGRESS_TARGETS}

.include <bsd.regress.mk>
