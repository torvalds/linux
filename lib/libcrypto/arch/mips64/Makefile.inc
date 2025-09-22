# $OpenBSD: Makefile.inc,v 1.19 2025/02/14 12:01:58 jsing Exp $

# mips64-specific libcrypto build rules

# aes
CFLAGS+= -DAES_ASM
SSLASM+= aes aes-mips aes-mips
# bn
SSLASM+= bn mips bn-mips
SSLASM+= bn mips-mont mips-mont
CFLAGS+= -DOPENSSL_BN_ASM_MONT
# sha
SSLASM+= sha sha1-mips sha1-mips
SSLASM+= sha sha512-mips sha256-mips
SSLASM+= sha sha512-mips sha512-mips

.for dir src dst in ${SSLASM}
SRCS+=	${dst}.S
GENERATED+=${dst}.S
${dst}.S: ${LCRYPTO_SRC}/${dir}/asm/${src}.pl
	/usr/bin/env CC=${CC} /usr/bin/perl \
		${LCRYPTO_SRC}/${dir}/asm/${src}.pl 64 ${.TARGET} > ${.TARGET}
.endfor
