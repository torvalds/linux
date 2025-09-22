# $OpenBSD: Makefile,v 1.3 2020/10/17 16:31:20 tb Exp $

# Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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

CLEANFILES += ktrace.out server.der client.der revoked.der

ocsp-start: 
	/usr/bin/openssl ocsp -port 127.0.0.1:2560 -text -sha256  -index ${.CURDIR}/index.txt -CA ${.CURDIR}/chain.pem -rkey ${.CURDIR}/ocsp-localhost.key.pem -rsigner ${.CURDIR}/ocsp-localhost.pem -nmin 1 > /dev/null 2>&1 &
	sleep .1 # give openssl some time to spin up
	pgrep -xf '/usr/bin/openssl ocsp -port 127.0.0.1:2560.*'

ocsp-stop:
	pkill -xf '/usr/bin/openssl ocsp -port 127.0.0.1:2560.*'

REGRESS_TARGETS +=	run-regress-ocspcheck
run-regress-ocspcheck: ocsp-start
	/usr/sbin/ocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -o ${.OBJDIR}/server.der \
	    ${.CURDIR}/server.crt
	/usr/sbin/ocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -o ${.OBJDIR}/client.der \
	    ${.CURDIR}/client.crt
	/usr/sbin/ocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.OBJDIR}/server.der \
	    ${.CURDIR}/server.crt
	/usr/sbin/ocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.OBJDIR}/client.der \
	    ${.CURDIR}/client.crt
	${.CURDIR}/failocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -o ${.OBJDIR}/revoked.der \
	    ${.CURDIR}/revoked.crt
	${.CURDIR}/failocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.OBJDIR}/revoked.der \
	    ${.CURDIR}/revoked.crt
	${.CURDIR}/failocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.OBJDIR}/client.der \
	    ${.CURDIR}/revoked.crt
	${.CURDIR}/failocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.OBJDIR}/server.der \
	    ${.CURDIR}/client.crt
	${.CURDIR}/failocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.OBJDIR}/revoked.der \
	    ${.CURDIR}/client.crt
	${.CURDIR}/failocspcheck -v \
	    -C ${.CURDIR}/chain.pem \
	    -i ${.CURDIR}/old-client-staple.der \
	    ${.CURDIR}/client.crt

REGRESS_TARGETS +=	run-regress-cleanup
run-regress-cleanup:
	${.MAKE} -C ${.CURDIR} ocsp-stop

.PHONY: ${REGRESS_TARGETS} ocsp-start ocsp-stop

.include <bsd.regress.mk>
