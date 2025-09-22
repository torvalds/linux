# $OpenBSD: Makefile,v 1.12 2025/07/25 16:33:15 tb Exp $

.include <bsd.own.mk>

.if ! exists(/usr/local/bin/botan)
regress:
	# install botan2 from ports for interop tests
	@echo 'Run "pkg_add botan2" to run tests against Botan 2'
	@echo SKIPPED
.elif (${COMPILER_VERSION:L} != "clang" && ! exists(/usr/local/bin/eg++))
regress:
	# on gcc-archs install g++ from ports for botan2 interop tests
	@echo 'Run "pkg_add g++" to run tests against Botan 2 on GCC architectures'
	@echo SKIPPED
.else

# C++11
.if ${COMPILER_VERSION:L} != "clang" && ${CXX} == "c++"
CXX = /usr/local/bin/eg++
.endif

LIBRARIES =		libressl
.if exists(/usr/local/bin/eopenssl35)
LIBRARIES +=		openssl35
.endif

PROGS =		client
SRCS_client =	client.cpp
CXXFLAGS =	-I/usr/local/include/botan-2 -Wall
LDFLAGS =	-L/usr/local/lib
LDADD =		-lbotan-2
DPADD =		/usr/local/lib/libbotan-2.a

.for lib in ${LIBRARIES}

REGRESS_TARGETS += run-client-botan-server-${lib}

run-client-botan-server-${lib}: client server.crt
	LD_LIBRARY_PATH=/usr/local/lib/e${lib} \
	    ../${lib}/server >server-${lib}.out \
	    -c server.crt -k server.key \
	    127.0.0.1 0
	./client >client-botan.out \
	    -C ca.crt \
	    127.0.0.1 \
	    `sed -n 's/listen sock: 127.0.0.1 //p' server-${lib}.out`
	# check that the server child run successfully to the end
	grep -q '^success$$' server-${lib}.out || \
	    { sleep 1; grep -q '^success$$' server-${lib}.out; }
	# server must have read client hello
	grep -q '^<<< hello$$' server-${lib}.out
	# check that the client run successfully to the end
	grep -q '^success$$' client-botan.out
	# client must have read server greeting
	grep -q '^<<< greeting$$' client-botan.out
	# currently botan supports TLS 1.2, adapt later
	grep -q ' Protocol *: TLSv1.2$$' server-${lib}.out

.endfor

server.key ca.key:
	/usr/local/bin/botan keygen >$@.tmp
	mv $@.tmp $@

ca.crt: ${@:R}.key
	/usr/local/bin/botan gen_self_signed ${@:R}.key ${@:R} >$@.tmp \
	    --organization=tls-regress --ca
	mv $@.tmp $@

server.req: ${@:R}.key
	/usr/local/bin/botan gen_pkcs10 ${@:R}.key localhost >$@.tmp \
	    --organization=tls-regress --dns=127.0.0.1
	mv $@.tmp $@

server.crt: ca.crt ${@:R}.req
	/usr/local/bin/botan sign_cert ca.crt ca.key ${@:R}.req >$@.tmp
	mv $@.tmp $@

.endif # exists(/usr/local/bin/botan)

.include <bsd.regress.mk>
