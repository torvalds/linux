# $OpenBSD: Makefile,v 1.9 2022/11/13 14:09:17 tb Exp $

.PATH:	${.CURDIR}/man ${.CURDIR}/src

CFLAGS+= -I${.CURDIR}/src -std=c99
CFLAGS+= -D_FIDO_INTERNAL -DHAVE_UNISTD_H

WARNINGS=	yes
CDIAGFLAGS+=	-Wall -Wextra
CDIAGFLAGS+=	-Werror

LDADD+=		-L${BSDOBJDIR}/lib/libcbor -lcbor
LDADD+=		-L${BSDOBJDIR}/lib/libcrypto -lcrypto
LDADD+=		-L${BSDOBJDIR}/lib/libz -lz

SYMBOL_LIST=	Symbols.list
VERSION_SCRIPT=	Symbols.map
CLEANFILES=	${SYMBOL_LIST} ${VERSION_SCRIPT}

LIB=	fido2
SRCS=	aes256.c assert.c authkey.c bio.c blob.c buf.c cbor.c compress.c
SRCS+=	config.c cred.c credman.c dev.c ecdh.c eddsa.c err.c es256.c
SRCS+=	hid.c hid_openbsd.c hid_unix.c info.c io.c iso7816.c largeblob.c
SRCS+=	log.c nfc.c pin.c random.c reset.c rs1.c rs256.c time.c touch.c
SRCS+=	tpm.c types.c u2f.c util.c

HDRS=	fido.h fido/bio.h fido/config.h fido/credman.h fido/eddsa.h fido/err.h
HDRS+=	fido/es256.h fido/param.h fido/rs256.h fido/types.h

MAN=	eddsa_pk_new.3 es256_pk_new.3 fido_assert_allow_cred.3 fido_assert_new.3
MAN+=	fido_assert_set_authdata.3 fido_assert_verify.3 fido_bio_dev_get_info.3
MAN+=	fido_bio_enroll_new.3 fido_bio_info_new.3 fido_bio_template.3
MAN+=	fido_cbor_info_new.3 fido_cred_exclude.3 fido_cred_new.3
MAN+=	fido_cred_set_authdata.3 fido_cred_verify.3 fido_credman_metadata_new.3
MAN+=	fido_dev_get_assert.3 fido_dev_info_manifest.3 fido_dev_make_cred.3
MAN+=	fido_dev_open.3 fido_dev_set_io_functions.3 fido_dev_set_pin.3
MAN+=	fido_init.3 fido_strerr.3 rs256_pk_new.3 fido_dev_get_touch_begin.3
MAN+=	fido_dev_enable_entattest.3 fido_dev_largeblob_get.3

includes:
	@for i in $(HDRS); do \
		j="test -d ${DESTDIR}/usr/include/`dirname $$i` || \
		    ${INSTALL} -d -o ${BINOWN} -g ${BINGRP} -m 755 \
		    ${DESTDIR}/usr/include/`dirname $$i`"; \
		echo $$j; \
		eval "$$j"; \
		j="cmp -s src/$$i ${DESTDIR}/usr/include/$$i || \
		    ${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} \
		    -m 444 ${.CURDIR}/src/$$i ${DESTDIR}/usr/include/$$i"; \
		echo $$j; \
		eval "$$j"; \
	done

${SYMBOL_LIST}: ${.CURDIR}/src/export.llvm
	sort ${.CURDIR}/src/export.llvm | sed 's/^_//' > $@.tmp && mv $@.tmp $@

${VERSION_SCRIPT}: ${SYMBOL_LIST}
	{ printf '{\n\tglobal:\n'; \
	  sed '/^[._a-zA-Z]/s/$$/;/; s/^/		/' ${SYMBOL_LIST}; \
	  printf '\n\tlocal:\n\t\t*;\n};\n'; } >$@.tmp && mv $@.tmp $@

.include <bsd.lib.mk>
