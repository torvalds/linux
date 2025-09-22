# $OpenBSD: Makefile.inc,v 1.31 2025/06/28 12:39:10 jsing Exp $

# i386-specific libcrypto build rules

# all i386 code generators use these
EXTRA_PL =  ${LCRYPTO_SRC}/perlasm/x86gas.pl ${LCRYPTO_SRC}/perlasm/x86asm.pl

SRCS += crypto_cpu_caps.c

# aes
CFLAGS+= -DAES_ASM
SSLASM+= aes aes-586
SSLASM+= aes aesni-x86
SRCS += aes_i386.c

# bn
SSLASM+= bn bn-586
SSLASM+= bn co-586
CFLAGS+= -DOPENSSL_BN_ASM_MONT
SSLASM+= bn x86-mont

# md5
CFLAGS+= -DMD5_ASM
SSLASM+= md5 md5-586

# modes
CFLAGS+= -DGHASH_ASM
SSLASM+= modes ghash-x86
SRCS += gcm128_i386.c

# rc4
SSLASM+= rc4 rc4-586

# sha
SSLASM+= sha sha1-586
SSLASM+= sha sha256-586
SSLASM+= sha sha512-586

.for dir f in ${SSLASM}
SRCS+=	${f}.S
GENERATED+=${f}.S
${f}.S: ${LCRYPTO_SRC}/${dir}/asm/${f}.pl ${EXTRA_PL}
	/usr/bin/perl -I${LCRYPTO_SRC}/perlasm -I${LCRYPTO_SRC}/${dir}/asm \
		${LCRYPTO_SRC}/${dir}/asm/${f}.pl \
		    openbsd-elf ${CFLAGS} 386 ${PICFLAG} > ${.TARGET}
.endfor
