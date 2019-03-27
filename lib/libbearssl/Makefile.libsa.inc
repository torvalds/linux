# $FreeBSD$

# This file is included by libsa
# It contains SRCS needed for loader

.PATH: ${.PARSEDIR}

.include "Makefile.inc"

.PATH: ${BEARSSL_SRC}

CFLAGS+=  -I${BEARSSL_SRC}

# we do not need/want nested objdirs
OBJS_SRCS_FILTER = T R

# we list only the srcs the loader(s) actually needs
SRCS+= \
	codec/ccopy.c \
	codec/dec32be.c \
	codec/dec64be.c \
	codec/enc32be.c \
	codec/enc64be.c \
	codec/pemdec.c \

SRCS+= \
	ec/ec_all_m31.c \
	ec/ec_c25519_m31.c \
	ec/ec_c25519_m62.c \
	ec/ec_c25519_m64.c \
	ec/ec_default.c \
	ec/ec_p256_m31.c \
	ec/ec_p256_m62.c \
	ec/ec_p256_m64.c \
	ec/ec_prime_i31.c \
	ec/ec_pubkey.c \
	ec/ec_secp256r1.c \
	ec/ec_secp384r1.c \
	ec/ec_secp521r1.c \
	ec/ecdsa_atr.c \
	ec/ecdsa_default_vrfy_asn1.c \
	ec/ecdsa_i31_bits.c \
	ec/ecdsa_i31_vrfy_asn1.c \
	ec/ecdsa_i31_vrfy_raw.c \

# Note: sha1 is needed for OpenPGP (keyId)
SRCS+= \
	hash/multihash.c \
	hash/sha1.c \
	hash/sha2big.c \
	hash/sha2small.c \

SRCS+= \
	int/i31_add.c \
	int/i31_bitlen.c \
	int/i31_decmod.c \
	int/i31_decode.c \
	int/i31_encode.c \
	int/i31_fmont.c \
	int/i31_iszero.c \
	int/i31_moddiv.c \
	int/i31_modpow.c \
	int/i31_modpow2.c \
	int/i31_montmul.c \
	int/i31_muladd.c \
	int/i31_ninv31.c \
	int/i31_rshift.c \
	int/i31_sub.c \
	int/i31_tmont.c \
	int/i32_div32.c \
	int/i62_modpow2.c \

SRCS+= \
	rsa/rsa_default_pkcs1_vrfy.c \
	rsa/rsa_i31_pkcs1_vrfy.c \
	rsa/rsa_i31_pub.c \
	rsa/rsa_i62_pkcs1_vrfy.c \
	rsa/rsa_i62_pub.c \
	rsa/rsa_pkcs1_sig_unpad.c \


SRCS+= \
	x509/asn1enc.c \
	x509/x509_decoder.c \
	x509/x509_minimal.c \

