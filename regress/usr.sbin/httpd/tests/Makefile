#	$OpenBSD: Makefile,v 1.16 2021/12/22 15:54:01 bluhm Exp $

# The following ports must be installed for the regression tests:
# p5-Socket6		Perl defines relating to AF_INET6 sockets
# p5-IO-Socket-SSL	perl interface to SSL sockets
#
# Check wether all required perl packages are installed.  If some
# are missing print a warning and skip the tests, but do not fail.

PERL_REQUIRE !=	perl -Mstrict -Mwarnings -e ' \
    eval { require Socket6 } or print $@; \
    eval { require IO::Socket::SSL } or print $@; \
'
.if ! empty (PERL_REQUIRE)
regress:
	@echo "${PERL_REQUIRE}"
	@echo 'run "pkg_add p5-Socket6 p5-IO-Socket-SSL"'
	@echo SKIPPED
.endif

# Automatically generate regress targets from test cases in directory.

ARGS !=			cd ${.CURDIR} && ls args-*.pl
CLEANFILES +=		*.log httpd.conf ktrace.out stamp-*
CLEANFILES +=		*.pem *.req *.crt *.key *.srl md5-*

HTDOCS_FILES =		512 1048576 1073741824
HTDOCS_MD5 =		${HTDOCS_FILES:S,^,md5-,}
HTDOCS_SPARSE =		yes
CLEANFILES +=		htdocs/*

# Set variables so that make runs with and without obj directory.
# Only do that if necessary to keep visible output short.

.if ${.CURDIR} == ${.OBJDIR}
PERLINC =	-I.
PERLPATH =
.else
PERLINC =	-I${.CURDIR}
PERLPATH =	${.CURDIR}/
.endif

# The arg tests take a perl hash with arguments controlling the
# test parameters.  Generally they consist of client, httpd, server.

.for a in ${ARGS}
REGRESS_TARGETS +=	run-$a
REGRESS_ROOT_TARGETS +=	run-$a
run-$a: $a ${HTDOCS_MD5}
	time SUDO="${SUDO}" KTRACE=${KTRACE} HTTPD=${HTTPD} perl ${PERLINC} ${PERLPATH}httpd.pl ${.OBJDIR} ${PERLPATH}$a
.endfor

# populate htdocs

.for d in ${HTDOCS_FILES}
htdocs/$d:
	mkdir -m 0755 -p ${@:H}
.if (${HTDOCS_SPARSE} != "yes")
	dd if=/dev/urandom of=$@ count=$$(($d / 512)) bs=512
.else
	dd of=$@ seek=$$(($d / 512)) bs=512 count=0 status=none
.endif

md5-$d: htdocs/$d
	md5 -q htdocs/$d >$@
.endfor

# create certificates for TLS

ca.crt:
	openssl req -batch -new -subj /L=OpenBSD/O=httpd-regress/OU=ca/CN=root/ -nodes -newkey rsa -keyout ca.key -x509 -out ca.crt

server.req:
	openssl req -batch -new -subj /L=OpenBSD/O=httpd-regress/OU=server/CN=localhost/ -nodes -newkey rsa -keyout server.key -out server.req

client.req:
	openssl req -batch -new -subj /L=OpenBSD/O=httpd-regress/OU=client/CN=localhost/ -nodes -newkey rsa -keyout client.key -out $@

server.crt: ca.crt server.req
	openssl x509 -CAcreateserial -CAkey ca.key -CA ca.crt -req -in server.req -out server.crt

client.crt: ca.crt client.req
	openssl x509 -CAcreateserial -CAkey ca.key -CA ca.crt -req -in client.req -out $@

${REGRESS_TARGETS:M*tls*} ${REGRESS_TARGETS:M*https*}: server.crt client.crt

# make perl syntax check for all args files

.PHONY: syntax

syntax: stamp-syntax

stamp-syntax: ${ARGS}
.for a in ${ARGS}
	@perl -c ${PERLPATH}$a
.endfor
	@date >$@

.include <bsd.regress.mk>
