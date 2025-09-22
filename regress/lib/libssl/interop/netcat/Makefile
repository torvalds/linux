# $OpenBSD: Makefile,v 1.12 2025/07/25 16:33:15 tb Exp $

LIBRARIES =		libressl
.if exists(/usr/local/bin/eopenssl35)
LIBRARIES +=		openssl35
.endif

# run netcat server and connect with test client

.for clib in ${LIBRARIES}

REGRESS_TARGETS +=	run-netcat-client-${clib}-server-nc
REGRESS_TARGETS +=	run-protocol-client-${clib}

run-netcat-client-${clib}-server-nc: ../${clib}/client 127.0.0.1.crt
	echo "greeting" | \
	    nc >${@:S/^run/server/}.out \
	    -l -c -C 127.0.0.1.crt -K 127.0.0.1.key \
	    127.0.0.1 0 & \
	    for i in `jot 1000`; do fstat -p $$! >netcat.fstat; \
	    grep -q ' stream tcp .*:[1-9][0-9]*$$' netcat.fstat && \
	    exit 0; done; exit 1
	LD_LIBRARY_PATH=/usr/local/lib/e${clib} \
	    ../${clib}/client >${@:S/^run/client/}.out \
	    `sed -n 's/.* stream tcp .*:/127.0.0.1 /p' netcat.fstat`
	# check that the client run successfully to the end
	grep -q '^success$$' ${@:S/^run/client/}.out
	# client must have read server greeting
	grep -q '^<<< greeting$$' ${@:S/^run/client/}.out
	# netstat server must have read client hello
	grep -q '^hello$$' ${@:S/^run/server/}.out

.endfor

# run test server and connect with netcat client

.for slib in ${LIBRARIES}

REGRESS_TARGETS +=	run-netcat-client-nc-server-${slib}

run-netcat-client-nc-server-${slib}: ../${slib}/server 127.0.0.1.crt
	LD_LIBRARY_PATH=/usr/local/lib/e${slib} \
	    ../${slib}/server >${@:S/^run/server/}.out \
	    127.0.0.1 0
	echo "hello" | \
	    nc >${@:S/^run/client/}.out \
	    -c -R 127.0.0.1.crt \
	    `sed -n 's/listen sock: //p' ${@:S/^run/server/}.out`
	# check that the server child run successfully to the end
	grep -q '^success$$' ${@:S/^run/server/}.out || \
	    { sleep 1; grep -q '^success$$' ${@:S/^run/server/}.out; }
	# server must have read client hello
	grep -q '^<<< hello$$' ${@:S/^run/server/}.out
	# client must have read server greeting
	grep -q '^greeting$$' ${@:S/^run/client/}.out

.endfor

# check the TLS protocol version in client and server logs

.for clib in ${LIBRARIES}

REGRESS_TARGETS +=	run-protocol-client-${clib}

run-protocol-client-${clib}: client-netcat-client-${clib}-server-nc.out
	# check that LibTLS protocol version is TLS 1.2 or TLS 1.3
	grep 'Protocol *: TLSv1.[23]' client-netcat-client-${clib}-server-nc.out

.endfor

.for slib in ${LIBRARIES}

REGRESS_TARGETS +=	run-protocol-server-${slib}

run-protocol-server-${slib}: server-netcat-client-nc-server-${slib}.out
	# check that LibTLS protocol version is TLS 1.2 or TLS 1.3
	grep 'Protocol *: TLSv1.[23]' server-netcat-client-nc-server-${slib}.out

.endfor

.include <bsd.regress.mk>
