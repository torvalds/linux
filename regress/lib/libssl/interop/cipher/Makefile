# $OpenBSD: Makefile,v 1.19 2025/07/25 16:33:15 tb Exp $

# Connect a client to a server.  Both can be current libressl, or
# openssl 1.1 or 3.0.  Create lists of supported ciphers
# and pin client and server to one of the ciphers.  Use server
# certificate with compatible type.  Check that client and server
# have used correct cipher by grepping in their session print out.

LIBRARIES =		libressl
.if exists(/usr/local/bin/eopenssl35)
LIBRARIES +=		openssl35
.endif

CLEANFILES =	*.tmp *.ciphers ciphers.mk

.for clib in ${LIBRARIES}
client-${clib}.ciphers:
	LD_LIBRARY_PATH=/usr/local/lib/e${clib} \
	    ../${clib}/client -l ALL -L >$@.tmp
	sed -n 's/^cipher //p' <$@.tmp | sort -u >$@
	rm $@.tmp
.endfor
.for slib in ${LIBRARIES}
server-${slib}.ciphers: 127.0.0.1.crt dsa.crt ec.crt rsa.crt
	LD_LIBRARY_PATH=/usr/local/lib/e${slib} \
	    ../${slib}/server -l ALL -L >$@.tmp
	sed -n 's/^cipher //p' <$@.tmp | sort -u >$@
	rm $@.tmp
.endfor

.for clib in ${LIBRARIES}
.for slib in ${LIBRARIES}
ciphers.mk: client-${clib}-server-${slib}.ciphers
client-${clib}-server-${slib}.ciphers: \
    client-${clib}.ciphers server-${slib}.ciphers client-libressl.ciphers
	# get ciphers shared between client and server
	sort client-${clib}.ciphers server-${slib}.ciphers >$@.tmp
	uniq -d <$@.tmp >$@
	# we are only interested in ciphers supported by libressl
	sort $@ client-libressl.ciphers >$@.tmp
. if "${clib}" == "openssl35" || "${slib}" == "openssl35"
	# OpenSSL's SSL_CTX_set_cipher_list doesn't accept TLSv1.3 ciphers
	sed -i '/^TLS_/d' $@.tmp
. endif
	uniq -d <$@.tmp >$@
	rm $@.tmp
.endfor
.endfor

ciphers.mk:
	rm -f $@ $@.tmp
.for clib in ${LIBRARIES}
.for slib in ${LIBRARIES}
	echo 'CIPHERS_${clib}_${slib} =' >>$@.tmp \
	    `cat client-${clib}-server-${slib}.ciphers`
.endfor
.endfor
	mv $@.tmp $@

# hack to convert generated lists into usable make variables
.if exists(ciphers.mk)
.include "ciphers.mk"
.else
regress: ciphers.mk
	${MAKE} -C ${.CURDIR} regress
.endif

LEVEL_libressl =
LEVEL_openssl35 = ,@SECLEVEL=0

.for clib in ${LIBRARIES}
.for slib in ${LIBRARIES}
.for cipher in ${CIPHERS_${clib}_${slib}}

.if "${cipher:M*-DSS-*}" != ""
TYPE_${cipher} =	dsa
.elif "${cipher:M*-ECDSA-*}" != ""
TYPE_${cipher} =	ec
.elif "${cipher:M*-RSA-*}" != ""
TYPE_${cipher} =	rsa
.else
TYPE_${cipher} =	127.0.0.1
.endif

DHPARAM_${cipher}_${slib} =

.if ("${clib}" == "libressl" || "${slib}" == "libressl")
REGRESS_TARGETS +=	run-cipher-${cipher}-client-${clib}-server-${slib}
.else
# Don't use REGRESS_SLOW_TARGETS since its handling in bsd.regress.mk is slow.
SLOW_TARGETS +=	run-cipher-${cipher}-client-${clib}-server-${slib}
.endif
run-cipher-${cipher}-client-${clib}-server-${slib} \
client-cipher-${cipher}-client-${clib}-server-${slib}.out \
server-cipher-${cipher}-client-${clib}-server-${slib}.out: dh.param \
    127.0.0.1.crt ${TYPE_${cipher}}.crt ../${clib}/client ../${slib}/server
	LD_LIBRARY_PATH=/usr/local/lib/e${slib} \
	    ../${slib}/server >${@:S/^run/server/}.out \
	    -c ${TYPE_${cipher}}.crt -k ${TYPE_${cipher}}.key \
	    -l ${cipher}${LEVEL_${slib}} ${DHPARAM_${cipher}_${slib}} \
	    127.0.0.1 0
	LD_LIBRARY_PATH=/usr/local/lib/e${clib} \
	    ../${clib}/client >${@:S/^run/client/}.out \
	    -l ${cipher}${LEVEL_${clib}} \
	    `sed -n 's/listen sock: //p' ${@:S/^run/server/}.out`
	grep -q '^success$$' ${@:S/^run/server/}.out || \
	    { sleep 1; grep -q '^success$$' ${@:S/^run/server/}.out; }
	grep -q '^success$$' ${@:S/^run/client/}.out

.if ("${clib}" == "libressl" || "${slib}" == "libressl")
REGRESS_TARGETS +=	check-cipher-${cipher}-client-${clib}-server-${slib}
.else
# Don't use REGRESS_SLOW_TARGETS since its handling in bsd.regress.mk is slow.
SLOW_TARGETS +=	check-cipher-${cipher}-client-${clib}-server-${slib}
.endif
check-cipher-${cipher}-client-${clib}-server-${slib}: \
    client-cipher-${cipher}-client-${clib}-server-${slib}.out \
    server-cipher-${cipher}-client-${clib}-server-${slib}.out
.if "${cipher:C/TLS_(AES.*_GCM|CHACHA.*_POLY.*)_SHA.*/TLS1_3/}" != TLS1_3
	# client and server 1.3 capable, not TLS 1.3 cipher
. if "${clib}" == "libressl"
	# libressl client may prefer chacha-poly if aes-ni is not supported
	egrep -q ' Cipher *: TLS_(AES_256_GCM_SHA384|CHACHA20_POLY1305_SHA256)$$' ${@:S/^check/client/}.out
. else
	# openssl 1.1 generic client cipher
	grep -q ' Cipher *: TLS_AES_256_GCM_SHA384$$' ${@:S/^check/client/}.out
. endif
. if "${clib}" == "libressl"
	# libressl client may prefer chacha-poly if aes-ni is not supported
.  if "${slib}" == "openssl35"
	egrep -q ' Cipher *: TLS_(AES_256_GCM_SHA384|CHACHA20_POLY1305_SHA256)$$' ${@:S/^check/server/}.out
.  else
	egrep -q ' Cipher *: TLS_(AES_256_GCM_SHA384|CHACHA20_POLY1305_SHA256)$$' ${@:S/^check/server/}.out
.  endif
. else
	# generic server cipher
	grep -q ' Cipher *: TLS_AES_256_GCM_SHA384$$' ${@:S/^check/server/}.out
. endif
.else
	grep -q ' Cipher *: ${cipher}$$' ${@:S/^check/client/}.out
	grep -q ' Cipher *: ${cipher}$$' ${@:S/^check/server/}.out
.endif

.endfor
.endfor
.endfor

.include <bsd.own.mk>
REGRESS_SKIP_SLOW ?= no
.if ${REGRESS_SKIP_SLOW:L} != "yes"
REGRESS_TARGETS += ${SLOW_TARGETS}
.endif

.include <bsd.regress.mk>
