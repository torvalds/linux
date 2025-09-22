# $OpenBSD: Makefile,v 1.5 2025/09/16 15:12:50 sthen Exp $

# Copyright (c) 2017 Alexander Bluhm <bluhm@openbsd.org>
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

# The following ports must be installed:
#
# pebble	small test server for RFC 8555 (ACME)

.if ! exists(/usr/local/bin/pebble)
regress:
	@echo Install pebble package to run this regress.
	@echo SKIPPED
.endif

clean: _SUBDIRUSE
	rm -f a.out [Ee]rrs mklog *.core y.tab.h ktrace.out pebble-config.json
	rm -f pebble.out
	rm -rf etc www

etc/acme-client.conf: acme-client.conf
	mkdir -p etc
	sed 's,$${.OBJDIR},${.OBJDIR},'\
		${.CURDIR}/acme-client.conf > etc/acme-client.conf

etc/acme-client-profile.conf: acme-client-profile.conf
	mkdir -p etc
	sed 's,$${.OBJDIR},${.OBJDIR},'\
		${.CURDIR}/acme-client-profile.conf \
		> etc/acme-client-profile.conf

etc/httpd.conf: httpd.conf
	mkdir -p etc
	sed 's,$${.OBJDIR},${.OBJDIR},'\
		${.CURDIR}/httpd.conf >etc/httpd.conf
	mkdir -p www/htdocs www/acme www/logs etc/acme etc/ssl/acme/private

httpd-start: etc/httpd.conf
	${SUDO} /usr/sbin/httpd -f ${.OBJDIR}/etc/httpd.conf
	sleep .1 # give httpd some time to spin up
	pgrep -xf "/usr/sbin/httpd -f ${.OBJDIR}/etc/httpd.conf"

httpd-stop:
	-${SUDO} pkill -xf "/usr/sbin/httpd -f ${.OBJDIR}/etc/httpd.conf"

pebble-config.json:
	sed 's,$${.CURDIR},${.CURDIR},'\
		${.CURDIR}/pebble-config.json > pebble-config.json

pebble-start: pebble-config.json
	/usr/local/bin/pebble -config ${.OBJDIR}/pebble-config.json > \
	    ${.OBJDIR}/pebble.out &
	while ! $$(fgrep -q 'Root CA certificate available' \
	    ${.OBJDIR}/pebble.out); do sleep .1; done

pebble-stop:
	pkill -xf "/usr/local/bin/pebble -config ${.OBJDIR}/pebble-config.json"

REGRESS_TARGETS +=	run-regress-acme
run-regress-acme: etc/acme-client.conf httpd-start pebble-start
	${SUDO} /usr/sbin/acme-client \
	    -f ${.OBJDIR}/etc/acme-client.conf \
	    -v localhost
	${SUDO} /usr/sbin/acme-client \
	    -f ${.OBJDIR}/etc/acme-client.conf \
	    -r -v localhost

REGRESS_TARGETS +=	run-regress-acme-profile
run-regress-acme-profile: etc/acme-client-profile.conf
	${SUDO} /usr/sbin/acme-client \
	    -f ${.OBJDIR}/etc/acme-client-profile.conf \
	    -v localhost
	${SUDO} /usr/sbin/acme-client \
	    -f ${.OBJDIR}/etc/acme-client-profile.conf \
	    -r -v localhost

REGRESS_TARGETS +=	run-regress-cleanup
run-regress-cleanup:
	${.MAKE} -C ${.CURDIR} httpd-stop
	${.MAKE} -C ${.CURDIR} pebble-stop

.PHONY: ${REGRESS_TARGETS} httpd-start httpd-stop pebble-start pebble-stop

.include <bsd.regress.mk>
