#	$OpenBSD: Makefile,v 1.4 2022/03/14 21:30:48 tb Exp $

CLEANFILES +=	*.pem *.serial *.txt *.attr *.old

# Start each regress run from scratch with new keys and CA database.
REGRESS_SETUP_ONCE +=	clean

REGRESS_SETUP_ONCE +=	root.serial intermediate.serial
root.serial intermediate.serial:
	echo 1000 >$@

REGRESS_SETUP_ONCE +=	root.txt intermediate.txt
root.txt intermediate.txt:
	true >$@

# Vanna Vanna make me a root cert
root.key.pem: stamp-clean
	# generate root rsa 4096 key
	openssl genrsa -out root.key.pem 4096

root.cert.pem: root.cnf root.key.pem \
    stamp-root.serial stamp-root.txt
	# generate root cert
	openssl req -batch -config ${.CURDIR}/root.cnf -key root.key.pem \
	    -new -x509 -days 365 -sha256 -extensions v3_ca -out root.cert.pem

# Make intermediate
intermediate.key.pem: stamp-clean
	# generate intermediate rsa 2048 key
	openssl genrsa -out intermediate.key.pem 2048

intermediate.csr.pem: intermediate.cnf intermediate.key.pem
	# generate intermediate req
	openssl req -batch -config ${.CURDIR}/intermediate.cnf -new -sha256 \
	  -key intermediate.key.pem -out intermediate.csr.pem

# Sign intermediate
intermediate.cert.pem: root.cnf root.cert.pem intermediate.csr.pem \
    stamp-intermediate.serial stamp-intermediate.txt
	# sign intermediate
	openssl ca -batch -config ${.CURDIR}/root.cnf \
	    -extensions v3_intermediate_ca -days 10 -notext -md sha256 \
	    -in intermediate.csr.pem -out intermediate.cert.pem

REGRESS_TARGETS +=	run-verify-intermediate
# Verify intermediate
run-verify-intermediate: root.cert.pem intermediate.cert.pem
	# validate intermediate CA
	openssl verify -CAfile root.cert.pem intermediate.cert.pem

chain.pem: intermediate.cert.pem root.cert.pem
	cat intermediate.cert.pem root.cert.pem > chain.pem

# Make a server certificate
server.key.pem: stamp-clean
	# genrsa server
	openssl genrsa -out server.key.pem 2048

server.csr.pem: intermediate.cnf server.key.pem
	# server req
	openssl req -batch -config ${.CURDIR}/intermediate.cnf -new -sha256 \
	    -subj '/CN=server.openbsd.org/OU=So and Sos/O=OpenBSD/C=CA' \
	    -key server.key.pem -out server.csr.pem

# Sign server key
server.cert.pem: intermediate.cnf intermediate.cert.pem server.csr.pem
	# server sign
	openssl ca -batch -config ${.CURDIR}/intermediate.cnf \
	    -extensions server_cert -days 5 -notext -md sha256 \
	    -in server.csr.pem -out server.cert.pem

# Make a client certificate
client.key.pem: stamp-clean
	# genrsa client
	openssl genrsa -out client.key.pem 2048

client.csr.pem: intermediate.cnf intermediate.cert.pem client.key.pem
	# client req
	openssl req -batch -config ${.CURDIR}/intermediate.cnf -new -sha256 \
	    -subj '/CN=client/OU=So and Sos/O=OpenBSD/C=CA' \
	    -key client.key.pem -out client.csr.pem

# Sign client key
client.cert.pem: intermediate.cnf intermediate.cert.pem client.csr.pem
	# client sign
	openssl ca -batch -config ${.CURDIR}/intermediate.cnf \
	    -extensions usr_cert -days 5 -notext -md sha256 \
	    -in client.csr.pem -out client.cert.pem

REGRESS_TARGETS +=	run-verify-server
# Verify server with intermediate
run-verify-server: chain.pem server.cert.pem
	# validate server cert
	openssl verify -purpose sslserver -CAfile chain.pem server.cert.pem

REGRESS_TARGETS +=	run-verify-client
# Verify client with intermediate
run-verify-client: chain.pem client.cert.pem
	# validate client cert
	openssl verify -purpose sslclient -CAfile chain.pem client.cert.pem

.include <bsd.regress.mk>
