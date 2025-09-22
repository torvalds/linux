# $OpenBSD: Makefile,v 1.85 2024/08/11 13:04:46 jsing Exp $

.include <bsd.own.mk>
.ifndef NOMAN
SUBDIR=	man
.endif

PC_FILES=openssl.pc libssl.pc

CLEANFILES=${PC_FILES} ${VERSION_SCRIPT}

LIB=	ssl

CFLAGS+= -Wall -Wundef
.if ${COMPILER_VERSION:L} == "clang"
CFLAGS+= -Werror -Wshadow
.endif
CFLAGS+= -DLIBRESSL_INTERNAL

CFLAGS+= -DLIBRESSL_NAMESPACE

.ifdef TLS1_3_DEBUG
CFLAGS+= -DTLS13_DEBUG
.endif
CFLAGS+= -I${.CURDIR}
CFLAGS+= -I${.CURDIR}/../libcrypto
CFLAGS+= -I${.CURDIR}/../libcrypto/arch/${MACHINE_CPU}
CFLAGS+= -I${.CURDIR}/../libcrypto/hidden
CFLAGS+= -I${.CURDIR}/../libcrypto/bio
CFLAGS+= -I${.CURDIR}/hidden

LDADD+= -L${BSDOBJDIR}/lib/libcrypto -lcrypto

VERSION_SCRIPT=	Symbols.map
SYMBOL_LIST=	${.CURDIR}/Symbols.list

SRCS= \
	bio_ssl.c \
	bs_ber.c \
	bs_cbb.c \
	bs_cbs.c \
	d1_both.c \
	d1_lib.c \
	d1_pkt.c \
	d1_srtp.c \
	pqueue.c \
	s3_cbc.c \
	s3_lib.c \
	ssl_asn1.c \
	ssl_both.c \
	ssl_cert.c \
	ssl_ciph.c \
	ssl_ciphers.c \
	ssl_clnt.c \
	ssl_err.c \
	ssl_init.c \
	ssl_kex.c \
	ssl_lib.c \
	ssl_methods.c \
	ssl_packet.c \
	ssl_pkt.c \
	ssl_rsa.c \
	ssl_seclevel.c \
	ssl_sess.c \
	ssl_sigalgs.c \
	ssl_srvr.c \
	ssl_stat.c \
	ssl_tlsext.c \
	ssl_transcript.c \
	ssl_txt.c \
	ssl_versions.c \
	t1_enc.c \
	t1_lib.c \
	tls12_key_schedule.c \
	tls12_lib.c \
	tls12_record_layer.c \
	tls13_client.c \
	tls13_error.c \
	tls13_handshake.c \
	tls13_handshake_msg.c \
	tls13_key_schedule.c \
	tls13_legacy.c \
	tls13_lib.c \
	tls13_quic.c \
	tls13_record.c \
	tls13_record_layer.c \
	tls13_server.c \
	tls_buffer.c \
	tls_content.c \
	tls_key_share.c \
	tls_lib.c

HDRS=	dtls1.h srtp.h ssl.h ssl3.h tls1.h

.PATH:	${.CURDIR}

includes:
	@test -d ${DESTDIR}/usr/include/openssl || \
	    mkdir ${DESTDIR}/usr/include/openssl
	@cd ${.CURDIR}; for i in $(HDRS); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/openssl/`basename $$i` || \
	    ${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 $$i\
		${DESTDIR}/usr/include/openssl"; \
	    echo $$j; \
	    eval "$$j"; \
	done;

${VERSION_SCRIPT}: ${SYMBOL_LIST}
	{ printf '{\n\tglobal:\n'; \
	  sed '/^[._a-zA-Z]/s/$$/;/; s/^/		/' ${SYMBOL_LIST}; \
	  printf '\n\tlocal:\n\t\t*;\n};\n'; } >$@.tmp && mv $@.tmp $@

.include <bsd.lib.mk>

all: ${PC_FILES}
${PC_FILES}: ${.CURDIR}/../libcrypto/opensslv.h
	/bin/sh ${.CURDIR}/generate_pkgconfig.sh -c ${.CURDIR} -o ${.OBJDIR}

beforeinstall:
	nm -o lib${LIB}.a | egrep -w 'printf|fprintf' && \
	    (echo please fix stdio usage in this library; false) || true
.for p in ${PC_FILES}
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.OBJDIR}/$p ${DESTDIR}/usr/lib/pkgconfig/
.endfor

.include <bsd.subdir.mk>
