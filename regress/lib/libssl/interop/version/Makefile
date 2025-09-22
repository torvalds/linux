# $OpenBSD: Makefile,v 1.12 2025/07/25 16:33:15 tb Exp $

# Connect a client to a server.  Both can be current libressl, or
# openssl 1.1 or openssl 3.0.  Pin client or server to a fixed TLS
# version number.  Incompatible versions must fail.  Check that client 
# and server have used correct version by grepping in their session
# print out.

LIBRARIES =		libressl
.if exists(/usr/local/bin/eopenssl35)
LIBRARIES +=		openssl35
.endif

VERSIONS =	any TLS1_2 TLS1_3

.for cver in ${VERSIONS}
.for sver in ${VERSIONS}

.if "${cver}" == any || "${sver}" == any || "${cver}" == "${sver}"
FAIL_${cver}_${sver} =
.else
FAIL_${cver}_${sver} = !
.endif

.for clib in ${LIBRARIES}
.for slib in ${LIBRARIES}

.if ("${cver}" != TLS1_3 && "${sver}" != TLS1_3) && \
    ((("${clib}" != openssl35 && "${slib}" != openssl35)) || \
    (("${cver}" != any && "${sver}" != any) && \
    ("${cver}" != TLS1 && "${sver}" != TLS1) && \
    ("${cver}" != TLS1_1 && "${sver}" != TLS1_1)))

.if ("${clib}" == "libressl" || "${slib}" == "libressl")
REGRESS_TARGETS +=	run-version-client-${clib}-${cver}-server-${slib}-${sver}
.else
# Don't use REGRESS_SLOW_TARGETS since its handling in bsd.regress.mk is slow.
SLOW_TARGETS +=	run-version-client-${clib}-${cver}-server-${slib}-${sver}
.endif

run-version-client-${clib}-${cver}-server-${slib}-${sver} \
client-version-client-${clib}-${cver}-server-${slib}-${sver}.out \
server-version-client-${clib}-${cver}-server-${slib}-${sver}.out: \
    127.0.0.1.crt ../${clib}/client ../${slib}/server
	LD_LIBRARY_PATH=/usr/local/lib/e${slib} \
	    ../${slib}/server >${@:S/^run/server/}.out \
	    -c 127.0.0.1.crt -k 127.0.0.1.key \
	    ${sver:Nany:S/^/-V /} \
	    127.0.0.1 0
	${FAIL_${cver}_${sver}} \
	LD_LIBRARY_PATH=/usr/local/lib/e${clib} \
	    ../${clib}/client >${@:S/^run/client/}.out \
	    ${cver:Nany:S/^/-V /} \
	    `sed -n 's/listen sock: //p' ${@:S/^run/server/}.out`
.if empty(${FAIL_${cver}_${sver}})
	grep -q '^success$$' ${@:S/^run/server/}.out || \
	    { sleep 1; grep -q '^success$$' ${@:S/^run/server/}.out; }
	grep -q '^success$$' ${@:S/^run/client/}.out
.endif

.if empty(${FAIL_${cver}_${sver}})

.if ("${clib}" == "libressl" || "${slib}" == "libressl")
REGRESS_TARGETS +=	check-version-client-${clib}-${cver}-server-${slib}-${sver}
.else
# Don't use REGRESS_SLOW_TARGETS since its handling in bsd.regress.mk is slow.
SLOW_TARGETS +=	check-version-client-${clib}-${cver}-server-${slib}-${sver}
.endif

check-version-client-${clib}-${cver}-server-${slib}-${sver}: \
    client-version-client-${clib}-${cver}-server-${slib}-${sver}.out \
    server-version-client-${clib}-${cver}-server-${slib}-${sver}.out
	@grep ' Protocol *: ' ${@:S/^check/client/}.out
	@grep ' Protocol *: ' ${@:S/^check/server/}.out
.if "${cver}" == any
.if "${sver}" == any
	grep -q ' Protocol *: TLSv1.3$$' ${@:S/^check/client/}.out
	grep -q ' Protocol *: TLSv1.3$$' ${@:S/^check/server/}.out
.else
	grep -q ' Protocol *: ${sver:S/TLS/TLSv/:S/_/./}$$' \
	    ${@:S/^check/client/}.out
	grep -q ' Protocol *: ${sver:S/TLS/TLSv/:S/_/./}$$' \
	    ${@:S/^check/server/}.out
.endif
.else
	grep -q ' Protocol *: ${cver:S/TLS/TLSv/:S/_/./}$$' \
	    ${@:S/^check/client/}.out
	grep -q ' Protocol *: ${cver:S/TLS/TLSv/:S/_/./}$$' \
	    ${@:S/^check/server/}.out
.endif
.endif

.endif

.endfor
.endfor
.endfor
.endfor

.include <bsd.own.mk>
REGRESS_SKIP_SLOW ?= no
.if ${REGRESS_SKIP_SLOW:L} != "yes"
REGRESS_TARGETS += ${SLOW_TARGETS}
.endif

.include <bsd.regress.mk>
