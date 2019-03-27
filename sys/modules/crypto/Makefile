# $FreeBSD$

LIBSODIUM=${SRCTOP}/sys/contrib/libsodium/src/libsodium

.PATH:	${SRCTOP}/sys/opencrypto
.PATH:	${SRCTOP}/sys/crypto
.PATH:	${SRCTOP}/sys/crypto/blowfish
.PATH:	${SRCTOP}/sys/crypto/camellia
.PATH:	${SRCTOP}/sys/crypto/des
.PATH:	${SRCTOP}/sys/crypto/rijndael
.PATH:	${SRCTOP}/sys/crypto/sha2
.PATH:	${SRCTOP}/sys/crypto/siphash
.PATH:	${SRCTOP}/sys/crypto/skein
.PATH:	${SRCTOP}/sys/crypto/blake2
.PATH:	${SRCTOP}/sys/crypto/chacha20
.PATH:	${SRCTOP}/sys/contrib/libb2
.PATH:	${LIBSODIUM}/crypto_onetimeauth/poly1305
.PATH:	${LIBSODIUM}/crypto_onetimeauth/poly1305/donna
.PATH:	${LIBSODIUM}/crypto_verify/sodium
.PATH:	${SRCTOP}/sys/crypto/libsodium

KMOD	= crypto
SRCS	= crypto.c cryptodev_if.c
SRCS	+= criov.c cryptosoft.c xform.c
SRCS	+= cast.c cryptodeflate.c rmd160.c rijndael-alg-fst.c rijndael-api.c rijndael-api-fst.c
SRCS	+= skipjack.c bf_enc.c bf_ecb.c bf_skey.c
SRCS	+= camellia.c camellia-api.c
SRCS	+= des_ecb.c des_enc.c des_setkey.c
SRCS	+= sha1.c sha256c.c sha512c.c
SRCS	+= skein.c skein_block.c
# unroll the 256 and 512 loops, half unroll the 1024
CFLAGS+= -DSKEIN_LOOP=995
.if exists(${MACHINE_ARCH}/skein_block_asm.s)
.PATH:	${SRCTOP}/sys/crypto/skein/${MACHINE_ARCH}
SRCS	+= skein_block_asm.s
CFLAGS	+= -DSKEIN_ASM -DSKEIN_USE_ASM=1792 # list of block functions to replace with assembly: 256+512+1024 = 1792
ACFLAGS	+= -DELF -Wa,--noexecstack
# Fully unroll all loops in the assembly optimized version
AFLAGS+= --defsym SKEIN_LOOP=0
.endif
SRCS	+= siphash.c
SRCS	+= gmac.c gfmult.c
SRCS	+= blake2b-ref.c
SRCS	+= blake2s-ref.c
SRCS	+= blake2-sw.c
CFLAGS.blake2b-ref.c	+= -I${SRCTOP}/sys/crypto/blake2 -DSUFFIX=_ref
CFLAGS.blake2s-ref.c	+= -I${SRCTOP}/sys/crypto/blake2 -DSUFFIX=_ref
CFLAGS.blake2-sw.c	+= -I${SRCTOP}/sys/crypto/blake2
CWARNFLAGS.blake2b-ref.c	+= -Wno-cast-qual -Wno-unused-function
CWARNFLAGS.blake2s-ref.c	+= -Wno-cast-qual -Wno-unused-function
SRCS	+= chacha.c
SRCS	+= chacha-sw.c

LIBSODIUM_INC=${LIBSODIUM}/include
LIBSODIUM_COMPAT=${SRCTOP}/sys/crypto/libsodium
SRCS	+= xform_poly1305.c
CFLAGS.xform_poly1305.c		+= -I${LIBSODIUM_INC} -I${LIBSODIUM_COMPAT}
SRCS	+= onetimeauth_poly1305.c
CFLAGS.onetimeauth_poly1305.c	+= -I${LIBSODIUM_INC}/sodium -I${LIBSODIUM_COMPAT}
SRCS	+= poly1305_donna.c
CFLAGS.poly1305_donna.c		+= -I${LIBSODIUM_INC}/sodium -I${LIBSODIUM_COMPAT}
SRCS	+= verify.c
CFLAGS.verify.c			+= -I${LIBSODIUM_INC}/sodium -I${LIBSODIUM_COMPAT}
SRCS	+= randombytes.c
CFLAGS.randombytes.c		+= -I${LIBSODIUM_INC} -I${LIBSODIUM_COMPAT}
SRCS	+= utils.c
CFLAGS.utils.c			+= -I${LIBSODIUM_INC} -I${LIBSODIUM_COMPAT}

SRCS	+= opt_param.h cryptodev_if.h bus_if.h device_if.h
SRCS	+= opt_ddb.h
SRCS	+= cbc_mac.c
SRCS	+= xform_cbc_mac.c

.include <bsd.kmod.mk>
