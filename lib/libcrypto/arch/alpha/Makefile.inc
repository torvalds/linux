# $OpenBSD: Makefile.inc,v 1.15 2025/02/14 12:01:58 jsing Exp $

# alpha-specific libcrypto build rules

# bn
SSLASM+= bn alpha-mont
CFLAGS+= -DOPENSSL_BN_ASM_MONT
# modes
CFLAGS+= -DGHASH_ASM
SSLASM+= modes ghash-alpha
# sha
SSLASM+= sha sha1-alpha

.for dir f in ${SSLASM}
SRCS+=	${f}.S
GENERATED+=${f}.S
${f}.S: ${LCRYPTO_SRC}/${dir}/asm/${f}.pl
	/usr/bin/perl \
		${LCRYPTO_SRC}/${dir}/asm/${f}.pl > ${.TARGET}
.endfor
