# $OpenBSD: Makefile,v 1.16 2025/07/25 16:33:15 tb Exp $

# Connect a client to a server.  Both can be current libressl, or
# openssl 3.x.  Create client and server certificates
# that are signed by a CA and not signed by a fake CA.  Try all
# combinations with, without, and with wrong CA for client and server
# and check the result of certificate verification.

LIBRARIES =		libressl
.if exists(/usr/local/bin/eopenssl35)
LIBRARIES +=		openssl35
.endif

.for cca in noca ca fakeca
.for sca in noca ca fakeca
.for ccert in nocert cert
.for scert in nocert cert
.for cv in noverify verify
.for sv in noverify verify certverify

# remember when certificate verification should fail
.if (("${cv}" == verify && "${cca}" == ca && "${scert}" == cert) || \
    "${cv}" == noverify) && \
    (("${sv}" == verify && "${ccert}" == nocert) || \
    ("${sv}" == verify && "${sca}" == ca && "${ccert}" == cert) || \
    ("${sv}" == certverify && "${sca}" == ca && "${ccert}" == cert) || \
    "${sv}" == noverify)
FAIL_${cca}_${sca}_${ccert}_${scert}_${cv}_${sv} =
.else
FAIL_${cca}_${sca}_${ccert}_${scert}_${cv}_${sv} = !
.endif

.for clib in ${LIBRARIES}
.for slib in ${LIBRARIES}

.if ("${clib}" == "libressl" || "${slib}" == "libressl")
REGRESS_TARGETS +=	run-cert-client-${clib}-${cca}-${ccert}-${cv}-server-${slib}-${sca}-${scert}-${sv}
.else
# Don't use REGRESS_SLOW_TARGETS since its handling in bsd.regress.mk is slow.
SLOW_TARGETS +=	run-cert-client-${clib}-${cca}-${ccert}-${cv}-server-${slib}-${sca}-${scert}-${sv}
.endif

run-cert-client-${clib}-${cca}-${ccert}-${cv}-server-${slib}-${sca}-${scert}-${sv}: \
    127.0.0.1.crt ca.crt fake-ca.crt client.crt server.crt \
    ../${clib}/client ../${slib}/server
	LD_LIBRARY_PATH=/usr/local/lib/e${slib} \
	    ../${slib}/server >${@:S/^run/server/}.out \
	    ${sca:S/^noca//:S/^fakeca/-C fake-ca.crt/:S/^ca/-C ca.crt/} \
	    ${scert:S/^nocert//:S/^cert/-c server.crt -k server.key/} \
	    ${sv:S/^noverify//:S/^verify/-v/:S/^certverify/-vv/} \
	    127.0.0.1 0
	${FAIL_${cca}_${sca}_${ccert}_${scert}_${cv}_${sv}} \
	    LD_LIBRARY_PATH=/usr/local/lib/e${clib} \
	    ../${clib}/client >${@:S/^run/client/}.out \
	    ${cca:S/^noca//:S/^fakeca/-C fake-ca.crt/:S/^ca/-C ca.crt/} \
	    ${ccert:S/^nocert//:S/^cert/-c server.crt -k server.key/} \
	    ${cv:S/^noverify//:S/^verify/-v/} \
	    `sed -n 's/listen sock: //p' ${@:S/^run/server/}.out`
.if empty(${FAIL_${cca}_${sca}_${ccert}_${scert}_${cv}_${sv}})
	grep '^success$$' ${@:S/^run/server/}.out || \
	    { sleep 1; grep '^success$$' ${@:S/^run/server/}.out; }
	grep '^success$$' ${@:S/^run/client/}.out
.elif ! ("${sv}" == certverify && "${ccert}" == nocert) || \
    ("${cv}" == verify && "${scert}" != cert)
	grep '^verify: fail' ${@:S/^run/client/}.out ${@:S/^run/server/}.out
.endif

.endfor
.endfor
.endfor
.endfor
.endfor
.endfor
.endfor
.endfor

.include <bsd.own.mk>
REGRESS_SKIP_SLOW ?= no
.if ${REGRESS_SKIP_SLOW:L} != "yes"
REGRESS_TARGETS += ${SLOW_TARGETS}
.endif

REGRESS_TARGETS +=	run-bob
run-bob:
	@echo Bob, be happy!  Tests finished.

# argument list too long for a single rm *

clean: _SUBDIRUSE
	rm -f client-*.out
	rm -f server-*.out
	rm -f a.out [Ee]rrs mklog *.core y.tab.h \
	    ${PROG} ${PROGS} ${OBJS} ${_LEXINTM} ${_YACCINTM} ${CLEANFILES}

.include <bsd.regress.mk>
