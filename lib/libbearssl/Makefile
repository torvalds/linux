# $FreeBSD$

# This is a reach over build of BearSSL (www.BearSSL.org)

.include <src.opts.mk>

LIB= bearssl

# pick up SRCS used by libsa
.include "Makefile.libsa.inc"

SRCS+= \
	codec/dec16be.c \
	codec/dec16le.c \
	codec/dec32le.c \
	codec/dec64le.c \
	codec/enc16be.c \
	codec/enc16le.c \
	codec/enc32le.c \
	codec/enc64le.c \
	codec/pemenc.c \
	ec/ec_all_m15.c \
	ec/ec_c25519_i15.c \
	ec/ec_c25519_i31.c \
	ec/ec_c25519_m15.c \
	ec/ec_curve25519.c \
	ec/ec_keygen.c \
	ec/ec_p256_m15.c \
	ec/ec_prime_i15.c \
	ec/ecdsa_default_sign_asn1.c \
	ec/ecdsa_default_sign_raw.c \
	ec/ecdsa_default_vrfy_raw.c \
	ec/ecdsa_i15_bits.c \
	ec/ecdsa_i15_sign_asn1.c \
	ec/ecdsa_i15_sign_raw.c \
	ec/ecdsa_i15_vrfy_asn1.c \
	ec/ecdsa_i15_vrfy_raw.c \
	ec/ecdsa_i31_sign_asn1.c \
	ec/ecdsa_i31_sign_raw.c \
	ec/ecdsa_rta.c \
	hash/dig_oid.c \
	hash/dig_size.c \
	hash/ghash_ctmul.c \
	hash/ghash_ctmul32.c \
	hash/ghash_ctmul64.c \
	hash/ghash_pwr8.c \
	hash/md5.c \
	hash/md5sha1.c \


# this one does not compile for amd64
# we do not need it
#	hash/ghash_pclmul.c \

SRCS+= \
	int/i15_add.c \
	int/i15_bitlen.c \
	int/i15_decmod.c \
	int/i15_decode.c \
	int/i15_decred.c \
	int/i15_encode.c \
	int/i15_fmont.c \
	int/i15_iszero.c \
	int/i15_moddiv.c \
	int/i15_modpow.c \
	int/i15_modpow2.c \
	int/i15_montmul.c \
	int/i15_mulacc.c \
	int/i15_muladd.c \
	int/i15_ninv15.c \
	int/i15_reduce.c \
	int/i15_rshift.c \
	int/i15_sub.c \
	int/i15_tmont.c \
	int/i31_decred.c \
	int/i31_mulacc.c \
	int/i31_reduce.c \
	int/i32_add.c \
	int/i32_bitlen.c \
	int/i32_decmod.c \
	int/i32_decode.c \
	int/i32_decred.c \
	int/i32_encode.c \
	int/i32_fmont.c \
	int/i32_iszero.c \
	int/i32_modpow.c \
	int/i32_montmul.c \
	int/i32_mulacc.c \
	int/i32_muladd.c \
	int/i32_ninv32.c \
	int/i32_reduce.c \
	int/i32_sub.c \
	int/i32_tmont.c \

SRCS+= \
	kdf/hkdf.c \
	kdf/shake.c \

SRCS+= \
	rsa/rsa_default_keygen.c \
	rsa/rsa_default_modulus.c \
	rsa/rsa_default_pkcs1_sign.c \
	rsa/rsa_default_priv.c \
	rsa/rsa_default_privexp.c \
	rsa/rsa_default_pss_sign.c \
	rsa/rsa_default_pss_vrfy.c \
	rsa/rsa_default_pub.c \
	rsa/rsa_default_pubexp.c \
	rsa/rsa_i15_keygen.c \
	rsa/rsa_i15_modulus.c \
	rsa/rsa_i15_pkcs1_sign.c \
	rsa/rsa_i15_pkcs1_vrfy.c \
	rsa/rsa_i15_priv.c \
	rsa/rsa_i15_privexp.c \
	rsa/rsa_i15_pss_sign.c \
	rsa/rsa_i15_pss_vrfy.c \
	rsa/rsa_i15_pub.c \
	rsa/rsa_i15_pubexp.c \
	rsa/rsa_i31_keygen.c \
	rsa/rsa_i31_keygen_inner.c \
	rsa/rsa_i31_modulus.c \
	rsa/rsa_i31_pkcs1_sign.c \
	rsa/rsa_i31_priv.c \
	rsa/rsa_i31_privexp.c \
	rsa/rsa_i31_pss_sign.c \
	rsa/rsa_i31_pss_vrfy.c \
	rsa/rsa_i31_pubexp.c \
	rsa/rsa_i32_pkcs1_sign.c \
	rsa/rsa_i32_pkcs1_vrfy.c \
	rsa/rsa_i32_priv.c \
	rsa/rsa_i32_pss_sign.c \
	rsa/rsa_i32_pss_vrfy.c \
	rsa/rsa_i32_pub.c \
	rsa/rsa_i62_keygen.c \
	rsa/rsa_i62_pkcs1_sign.c \
	rsa/rsa_i62_priv.c \
	rsa/rsa_i62_pss_sign.c \
	rsa/rsa_i62_pss_vrfy.c \
	rsa/rsa_pkcs1_sig_pad.c \
	rsa/rsa_pss_sig_pad.c \
	rsa/rsa_pss_sig_unpad.c \
	rsa/rsa_ssl_decrypt.c \

SRCS+= \
	x509/encode_ec_pk8der.c \
	x509/encode_ec_rawder.c \
	x509/encode_rsa_pk8der.c \
	x509/encode_rsa_rawder.c \
	x509/skey_decoder.c \
	x509/x509_knownkey.c \
	x509/x509_minimal_full.c \

INCS= \
	inc/bearssl.h \
	inc/bearssl_aead.h \
	inc/bearssl_block.h \
	inc/bearssl_ec.h \
	inc/bearssl_hash.h \
	inc/bearssl_hmac.h \
	inc/bearssl_kdf.h \
	inc/bearssl_pem.h \
	inc/bearssl_prf.h \
	inc/bearssl_rand.h \
	inc/bearssl_rsa.h \
	inc/bearssl_ssl.h \
	inc/bearssl_x509.h \

INCS:= ${INCS:S,^,${BEARSSL}/,}


.if ${MK_BEARSSL_SSL:Uno} == "yes"
SRCS+= \
	mac/hmac.c \
	mac/hmac_ct.c \
	rand/hmac_drbg.c \
	ssl/prf.c \
	ssl/prf_md5sha1.c \
	ssl/prf_sha256.c \
	ssl/prf_sha384.c \
	ssl/ssl_ccert_single_ec.c \
	ssl/ssl_ccert_single_rsa.c \
	ssl/ssl_client.c \
	ssl/ssl_client_default_rsapub.c \
	ssl/ssl_client_full.c \
	ssl/ssl_engine.c \
	ssl/ssl_engine_default_aescbc.c \
	ssl/ssl_engine_default_aesccm.c \
	ssl/ssl_engine_default_aesgcm.c \
	ssl/ssl_engine_default_chapol.c \
	ssl/ssl_engine_default_descbc.c \
	ssl/ssl_engine_default_ec.c \
	ssl/ssl_engine_default_ecdsa.c \
	ssl/ssl_engine_default_rsavrfy.c \
	ssl/ssl_hashes.c \
	ssl/ssl_hs_client.c \
	ssl/ssl_hs_server.c \
	ssl/ssl_io.c \
	ssl/ssl_lru.c \
	ssl/ssl_rec_cbc.c \
	ssl/ssl_rec_ccm.c \
	ssl/ssl_rec_chapol.c \
	ssl/ssl_rec_gcm.c \
	ssl/ssl_scert_single_ec.c \
	ssl/ssl_scert_single_rsa.c \
	ssl/ssl_server.c \
	ssl/ssl_server_full_ec.c \
	ssl/ssl_server_full_rsa.c \
	ssl/ssl_server_mine2c.c \
	ssl/ssl_server_mine2g.c \
	ssl/ssl_server_minf2c.c \
	ssl/ssl_server_minf2g.c \
	ssl/ssl_server_minr2g.c \
	ssl/ssl_server_minu2g.c \
	ssl/ssl_server_minv2g.c \

SRCS+= \
	symcipher/aes_big_cbcdec.c \
	symcipher/aes_big_cbcenc.c \
	symcipher/aes_big_ctr.c \
	symcipher/aes_big_dec.c \
	symcipher/aes_big_enc.c \
	symcipher/aes_common.c \
	symcipher/aes_ct.c \
	symcipher/aes_ct64.c \
	symcipher/aes_ct64_cbcdec.c \
	symcipher/aes_ct64_cbcenc.c \
	symcipher/aes_ct64_ctr.c \
	symcipher/aes_ct64_dec.c \
	symcipher/aes_ct64_enc.c \
	symcipher/aes_ct_cbcdec.c \
	symcipher/aes_ct_cbcenc.c \
	symcipher/aes_ct_ctr.c \
	symcipher/aes_ct_dec.c \
	symcipher/aes_ct_enc.c \
	symcipher/aes_pwr8.c \
	symcipher/aes_pwr8_cbcdec.c \
	symcipher/aes_pwr8_cbcenc.c \
	symcipher/aes_pwr8_ctr.c \
	symcipher/aes_pwr8_ctrcbc.c \
	symcipher/aes_small_cbcdec.c \
	symcipher/aes_small_cbcenc.c \
	symcipher/aes_small_ctr.c \
	symcipher/aes_small_dec.c \
	symcipher/aes_small_enc.c \
	symcipher/aes_x86ni.c \
	symcipher/aes_x86ni_cbcdec.c \
	symcipher/aes_x86ni_cbcenc.c \
	symcipher/aes_x86ni_ctr.c \
	symcipher/chacha20_ct.c \
	symcipher/des_ct.c \
	symcipher/des_ct_cbcdec.c \
	symcipher/des_ct_cbcenc.c \
	symcipher/des_support.c \
	symcipher/des_tab.c \
	symcipher/des_tab_cbcdec.c \
	symcipher/des_tab_cbcenc.c \
	symcipher/poly1305_ctmul.c \
	symcipher/poly1305_ctmul32.c \
	symcipher/poly1305_ctmulq.c \
	symcipher/poly1305_i15.c \

.endif

.include <bsd.lib.mk>

.if ${MACHINE} == "host"
CWARNFLAGS+= -Wno-error
.endif
XCFLAGS.gcc.rsa_i62_keygen+= -Wno-error
XCFLAGS.i15_moddiv+= -Wno-shadow
XCFLAGS.i31_moddiv+= -Wno-shadow
XCFLAGS.i62_modpow2+= -Wno-cast-align
XCFLAGS.x509_minimal+= -Wno-cast-qual

CFLAGS+= ${XCFLAGS.${.TARGET:T:R}:U} \
	${XCFLAGS.${COMPILER_TYPE}.${.TARGET:T:R}:U}

