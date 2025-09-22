#	$OpenBSD: Makefile,v 1.38 2023/05/05 21:23:02 tb Exp $

.include <bsd.own.mk>
.ifndef NOMAN
SUBDIR=	man
.endif

CFLAGS+= -Wall -Wimplicit -Wundef
.if ${COMPILER_VERSION:L} == "clang"
CFLAGS+= -Werror -Wshadow
.endif
CFLAGS+= -DLIBRESSL_INTERNAL

CLEANFILES= ${VERSION_SCRIPT}

WARNINGS= Yes

LIB=	tls

DPADD=	${LIBCRYPTO} ${LIBSSL}

LDADD+= -L${BSDOBJDIR}/lib/libcrypto -lcrypto
LDADD+= -L${BSDOBJDIR}/lib/libssl -lssl

VERSION_SCRIPT=	Symbols.map
SYMBOL_LIST=	${.CURDIR}/Symbols.list

HDRS=	tls.h

SRCS=	tls.c \
	tls_bio_cb.c \
	tls_client.c \
	tls_config.c \
	tls_conninfo.c \
	tls_keypair.c \
	tls_peer.c \
	tls_server.c \
	tls_signer.c \
	tls_util.c \
	tls_ocsp.c \
	tls_verify.c

includes:
	@cd ${.CURDIR}; for i in $(HDRS); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/$$i || \
	    ${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 $$i\
		${DESTDIR}/usr/include/"; \
	    echo $$j; \
	    eval "$$j"; \
	done;

${VERSION_SCRIPT}: ${SYMBOL_LIST}
	{ printf '{\n\tglobal:\n'; \
	  sed '/^[._a-zA-Z]/s/$$/;/; s/^/		/' ${SYMBOL_LIST}; \
	  printf '\n\tlocal:\n\t\t*;\n};\n'; } >$@.tmp && mv $@.tmp $@

.include <bsd.lib.mk>
