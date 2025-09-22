# $OpenBSD: Makefile,v 1.244 2025/09/05 23:30:12 beck Exp $

LIB=	crypto
LIBREBUILD=y

.include <bsd.own.mk>
.ifndef NOMAN
SUBDIR= man
.endif

PC_FILES=libcrypto.pc

CLEANFILES=${PC_FILES} ${VERSION_SCRIPT}

LCRYPTO_SRC=	${.CURDIR}

CFLAGS+= -Wall -Wundef
.if ${COMPILER_VERSION:L} == "clang"
CFLAGS+= -Werror -Wshadow
.endif
CFLAGS+= -DLIBRESSL_INTERNAL

CFLAGS+= -DLIBRESSL_NAMESPACE -DLIBRESSL_CRYPTO_NAMESPACE

CFLAGS+= -DHAVE_FUNOPEN

CFLAGS+= -I${LCRYPTO_SRC}
CFLAGS+= -I${LCRYPTO_SRC}/aes
CFLAGS+= -I${LCRYPTO_SRC}/arch/${MACHINE_CPU}
CFLAGS+= -I${LCRYPTO_SRC}/asn1
CFLAGS+= -I${LCRYPTO_SRC}/bio
CFLAGS+= -I${LCRYPTO_SRC}/bn
CFLAGS+= -I${LCRYPTO_SRC}/bn/arch/${MACHINE_CPU}
CFLAGS+= -I${LCRYPTO_SRC}/bytestring
CFLAGS+= -I${LCRYPTO_SRC}/conf
CFLAGS+= -I${LCRYPTO_SRC}/curve25519
CFLAGS+= -I${LCRYPTO_SRC}/dh
CFLAGS+= -I${LCRYPTO_SRC}/dsa
CFLAGS+= -I${LCRYPTO_SRC}/ec
CFLAGS+= -I${LCRYPTO_SRC}/ecdsa
CFLAGS+= -I${LCRYPTO_SRC}/err
CFLAGS+= -I${LCRYPTO_SRC}/evp
CFLAGS+= -I${LCRYPTO_SRC}/hidden
CFLAGS+= -I${LCRYPTO_SRC}/hmac
CFLAGS+= -I${LCRYPTO_SRC}/kdf
CFLAGS+= -I${LCRYPTO_SRC}/lhash
CFLAGS+= -I${LCRYPTO_SRC}/mlkem
CFLAGS+= -I${LCRYPTO_SRC}/modes
CFLAGS+= -I${LCRYPTO_SRC}/ocsp
CFLAGS+= -I${LCRYPTO_SRC}/pkcs12
CFLAGS+= -I${LCRYPTO_SRC}/rsa
CFLAGS+= -I${LCRYPTO_SRC}/sha
CFLAGS+= -I${LCRYPTO_SRC}/stack
CFLAGS+= -I${LCRYPTO_SRC}/ts
CFLAGS+= -I${LCRYPTO_SRC}/x509

VERSION_SCRIPT=	Symbols.map
SYMBOL_LIST=	${.CURDIR}/Symbols.list

# crypto/
SRCS+= crypto_err.c
SRCS+= crypto_ex_data.c
SRCS+= crypto_init.c
SRCS+= crypto_legacy.c
SRCS+= crypto_lock.c
SRCS+= crypto_memory.c

# aes/
SRCS+= aes.c
SRCS+= aes_core.c

# asn1/
SRCS+= a_bitstr.c
SRCS+= a_enum.c
SRCS+= a_int.c
SRCS+= a_mbstr.c
SRCS+= a_object.c
SRCS+= a_octet.c
SRCS+= a_pkey.c
SRCS+= a_print.c
SRCS+= a_pubkey.c
SRCS+= a_strex.c
SRCS+= a_string.c
SRCS+= a_strnid.c
SRCS+= a_time.c
SRCS+= a_time_posix.c
SRCS+= a_time_tm.c
SRCS+= a_type.c
SRCS+= a_utf8.c
SRCS+= asn1_err.c
SRCS+= asn1_gen.c
SRCS+= asn1_item.c
SRCS+= asn1_lib.c
SRCS+= asn1_old.c
SRCS+= asn1_old_lib.c
SRCS+= asn1_par.c
SRCS+= asn1_types.c
SRCS+= asn_mime.c
SRCS+= asn_moid.c
SRCS+= bio_asn1.c
SRCS+= bio_ndef.c
SRCS+= p5_pbe.c
SRCS+= p5_pbev2.c
SRCS+= p8_pkey.c
SRCS+= t_crl.c
SRCS+= t_req.c
SRCS+= t_spki.c
SRCS+= t_x509.c
SRCS+= t_x509a.c
SRCS+= tasn_dec.c
SRCS+= tasn_enc.c
SRCS+= tasn_fre.c
SRCS+= tasn_new.c
SRCS+= tasn_prn.c
SRCS+= tasn_typ.c
SRCS+= tasn_utl.c
SRCS+= x_algor.c
SRCS+= x_attrib.c
SRCS+= x_bignum.c
SRCS+= x_crl.c
SRCS+= x_exten.c
SRCS+= x_long.c
SRCS+= x_name.c
SRCS+= x_pubkey.c
SRCS+= x_req.c
SRCS+= x_sig.c
SRCS+= x_spki.c
SRCS+= x_val.c
SRCS+= x_x509.c
SRCS+= x_x509a.c

# bf/
SRCS+= blowfish.c

# bio/
SRCS+= b_dump.c
SRCS+= b_posix.c
SRCS+= b_print.c
SRCS+= b_sock.c
SRCS+= bf_buff.c
SRCS+= bf_nbio.c
SRCS+= bf_null.c
SRCS+= bio_cb.c
SRCS+= bio_err.c
SRCS+= bio_lib.c
SRCS+= bio_meth.c
SRCS+= bss_acpt.c
SRCS+= bss_bio.c
SRCS+= bss_conn.c
SRCS+= bss_dgram.c
SRCS+= bss_fd.c
SRCS+= bss_file.c
SRCS+= bss_mem.c
SRCS+= bss_null.c
SRCS+= bss_sock.c

# bn/
SRCS+= bn_add.c
SRCS+= bn_add_sub.c
SRCS+= bn_bpsw.c
SRCS+= bn_const.c
SRCS+= bn_convert.c
SRCS+= bn_ctx.c
SRCS+= bn_div.c
SRCS+= bn_err.c
SRCS+= bn_exp.c
SRCS+= bn_gcd.c
SRCS+= bn_isqrt.c
SRCS+= bn_kron.c
SRCS+= bn_lib.c
SRCS+= bn_mod.c
SRCS+= bn_mod_sqrt.c
SRCS+= bn_mod_words.c
SRCS+= bn_mont.c
SRCS+= bn_mul.c
SRCS+= bn_prime.c
SRCS+= bn_primitives.c
SRCS+= bn_print.c
SRCS+= bn_rand.c
SRCS+= bn_recp.c
SRCS+= bn_shift.c
SRCS+= bn_small_primes.c
SRCS+= bn_sqr.c
SRCS+= bn_word.c

# buffer/
SRCS+= buf_err.c
SRCS+= buffer.c

# bytestring/
SRCS+= bs_ber.c
SRCS+= bs_cbb.c
SRCS+= bs_cbs.c

# camellia/
SRCS+= camellia.c

# cast/
SRCS+= cast.c

# chacha/
SRCS+= chacha.c

# cmac/
SRCS+= cm_ameth.c
SRCS+= cm_pmeth.c
SRCS+= cmac.c

# cms/
SRCS+= cms_asn1.c
SRCS+= cms_att.c
SRCS+= cms_dd.c
SRCS+= cms_enc.c
SRCS+= cms_env.c
SRCS+= cms_err.c
SRCS+= cms_ess.c
SRCS+= cms_io.c
SRCS+= cms_kari.c
SRCS+= cms_lib.c
SRCS+= cms_pwri.c
SRCS+= cms_sd.c
SRCS+= cms_smime.c

# conf/
SRCS+= conf_api.c
SRCS+= conf_def.c
SRCS+= conf_err.c
SRCS+= conf_lib.c
SRCS+= conf_mod.c
SRCS+= conf_sap.c

# ct/
SRCS+= ct_b64.c
SRCS+= ct_err.c
SRCS+= ct_log.c
SRCS+= ct_oct.c
SRCS+= ct_policy.c
SRCS+= ct_prn.c
SRCS+= ct_sct.c
SRCS+= ct_sct_ctx.c
SRCS+= ct_vfy.c
SRCS+= ct_x509v3.c

# curve25519/
SRCS+= curve25519-generic.c
SRCS+= curve25519.c

# des/
SRCS+= des.c
SRCS+= des_cksum.c
SRCS+= des_enc.c
SRCS+= des_fcrypt.c
SRCS+= des_key.c

# dh/
SRCS+= dh_ameth.c
SRCS+= dh_asn1.c
SRCS+= dh_check.c
SRCS+= dh_err.c
SRCS+= dh_gen.c
SRCS+= dh_key.c
SRCS+= dh_lib.c
SRCS+= dh_pmeth.c

# dsa/
SRCS+= dsa_ameth.c
SRCS+= dsa_asn1.c
SRCS+= dsa_err.c
SRCS+= dsa_gen.c
SRCS+= dsa_key.c
SRCS+= dsa_lib.c
SRCS+= dsa_meth.c
SRCS+= dsa_ossl.c
SRCS+= dsa_pmeth.c
SRCS+= dsa_prn.c

# ec/
SRCS+= ec_ameth.c
SRCS+= ec_asn1.c
SRCS+= ec_convert.c
SRCS+= ec_curve.c
SRCS+= ec_err.c
SRCS+= ec_field.c
SRCS+= ec_key.c
SRCS+= ec_lib.c
SRCS+= ec_mult.c
SRCS+= ec_pmeth.c
SRCS+= eck_prn.c
SRCS+= ecp_hp_methods.c
SRCS+= ecp_methods.c
SRCS+= ecx_methods.c

# ecdh/
SRCS+= ecdh.c

# ecdsa/
SRCS+= ecdsa.c

# engine/
SRCS+= engine_stubs.c

# err/
SRCS+= err.c
SRCS+= err_all.c
SRCS+= err_prn.c

# evp/
SRCS+= bio_b64.c
SRCS+= bio_enc.c
SRCS+= bio_md.c
SRCS+= e_aes.c
SRCS+= e_bf.c
SRCS+= e_camellia.c
SRCS+= e_cast.c
SRCS+= e_chacha.c
SRCS+= e_chacha20poly1305.c
SRCS+= e_des.c
SRCS+= e_des3.c
SRCS+= e_idea.c
SRCS+= e_null.c
SRCS+= e_rc2.c
SRCS+= e_rc4.c
SRCS+= e_sm4.c
SRCS+= e_xcbc_d.c
SRCS+= evp_aead.c
SRCS+= evp_cipher.c
SRCS+= evp_digest.c
SRCS+= evp_encode.c
SRCS+= evp_err.c
SRCS+= evp_key.c
SRCS+= evp_names.c
SRCS+= evp_pbe.c
SRCS+= evp_pkey.c
SRCS+= m_md4.c
SRCS+= m_md5.c
SRCS+= m_md5_sha1.c
SRCS+= m_null.c
SRCS+= m_ripemd.c
SRCS+= m_sha1.c
SRCS+= m_sha3.c
SRCS+= m_sigver.c
SRCS+= m_sm3.c
SRCS+= p_legacy.c
SRCS+= p_lib.c
SRCS+= p_sign.c
SRCS+= p_verify.c
SRCS+= pmeth_fn.c
SRCS+= pmeth_gn.c
SRCS+= pmeth_lib.c

# hkdf/
SRCS+= hkdf.c

# hmac/
SRCS+= hm_ameth.c
SRCS+= hm_pmeth.c
SRCS+= hmac.c

# idea/
SRCS+= idea.c

# kdf/
SRCS+= hkdf_evp.c
SRCS+= kdf_err.c
SRCS+= tls1_prf.c

# lhash/
SRCS+= lhash.c

# md4/
SRCS+= md4.c

# md5/
SRCS+= md5.c

# mlkem/
SRCS+= mlkem.c
SRCS+= mlkem_internal.c
SRCS+= mlkem_key.c

# modes/
SRCS+= cbc128.c
SRCS+= ccm128.c
SRCS+= cfb128.c
SRCS+= ctr128.c
SRCS+= gcm128.c
SRCS+= ofb128.c
SRCS+= xts128.c

# objects/
SRCS+= obj_dat.c
SRCS+= obj_err.c
SRCS+= obj_lib.c
SRCS+= obj_xref.c

# ocsp/
SRCS+= ocsp_asn.c
SRCS+= ocsp_cl.c
SRCS+= ocsp_err.c
SRCS+= ocsp_ext.c
SRCS+= ocsp_ht.c
SRCS+= ocsp_lib.c
SRCS+= ocsp_prn.c
SRCS+= ocsp_srv.c
SRCS+= ocsp_vfy.c

# pem/
SRCS+= pem_all.c
SRCS+= pem_err.c
SRCS+= pem_info.c
SRCS+= pem_lib.c
SRCS+= pem_oth.c
SRCS+= pem_pk8.c
SRCS+= pem_pkey.c
SRCS+= pem_sign.c
SRCS+= pem_x509.c
SRCS+= pem_xaux.c
SRCS+= pvkfmt.c

# pkcs12/
SRCS+= p12_add.c
SRCS+= p12_asn.c
SRCS+= p12_attr.c
SRCS+= p12_crt.c
SRCS+= p12_decr.c
SRCS+= p12_init.c
SRCS+= p12_key.c
SRCS+= p12_kiss.c
SRCS+= p12_mutl.c
SRCS+= p12_npas.c
SRCS+= p12_p8d.c
SRCS+= p12_p8e.c
SRCS+= p12_sbag.c
SRCS+= p12_utl.c
SRCS+= pk12err.c

# pkcs7/
SRCS+= pk7_asn1.c
SRCS+= pk7_attr.c
SRCS+= pk7_doit.c
SRCS+= pk7_lib.c
SRCS+= pk7_mime.c
SRCS+= pk7_smime.c
SRCS+= pkcs7err.c

# poly1305/
SRCS+= poly1305.c

# rand/
SRCS+= rand_err.c
SRCS+= rand_lib.c
SRCS+= randfile.c

# rc2/
SRCS+= rc2.c

# rc4/
SRCS+= rc4.c

# ripemd/
SRCS+= ripemd.c

# rsa/
SRCS+= rsa_ameth.c
SRCS+= rsa_asn1.c
SRCS+= rsa_blinding.c
SRCS+= rsa_chk.c
SRCS+= rsa_eay.c
SRCS+= rsa_err.c
SRCS+= rsa_gen.c
SRCS+= rsa_lib.c
SRCS+= rsa_meth.c
SRCS+= rsa_none.c
SRCS+= rsa_oaep.c
SRCS+= rsa_pk1.c
SRCS+= rsa_pmeth.c
SRCS+= rsa_prn.c
SRCS+= rsa_pss.c
SRCS+= rsa_saos.c
SRCS+= rsa_sign.c
SRCS+= rsa_x931.c

# sha/
SRCS+= sha1.c
SRCS+= sha256.c
SRCS+= sha3.c
SRCS+= sha512.c

# sm3/
SRCS+= sm3.c

# sm4/
SRCS+= sm4.c

# stack/
SRCS+= stack.c

# ts/
SRCS+= ts_asn1.c
SRCS+= ts_conf.c
SRCS+= ts_err.c
SRCS+= ts_lib.c
SRCS+= ts_req_print.c
SRCS+= ts_req_utils.c
SRCS+= ts_rsp_print.c
SRCS+= ts_rsp_sign.c
SRCS+= ts_rsp_utils.c
SRCS+= ts_rsp_verify.c
SRCS+= ts_verify_ctx.c

# txt_db/
SRCS+=txt_db.c

# ui/
SRCS+= ui_err.c
SRCS+= ui_lib.c
SRCS+= ui_null.c
SRCS+= ui_openssl.c

# x509/
SRCS+= by_dir.c
SRCS+= by_file.c
SRCS+= by_mem.c
SRCS+= x509_addr.c
SRCS+= x509_akey.c
SRCS+= x509_akeya.c
SRCS+= x509_alt.c
SRCS+= x509_asid.c
SRCS+= x509_att.c
SRCS+= x509_bcons.c
SRCS+= x509_bitst.c
SRCS+= x509_cmp.c
SRCS+= x509_conf.c
SRCS+= x509_constraints.c
SRCS+= x509_cpols.c
SRCS+= x509_crld.c
SRCS+= x509_d2.c
SRCS+= x509_def.c
SRCS+= x509_err.c
SRCS+= x509_ext.c
SRCS+= x509_extku.c
SRCS+= x509_genn.c
SRCS+= x509_ia5.c
SRCS+= x509_info.c
SRCS+= x509_int.c
SRCS+= x509_issuer_cache.c
SRCS+= x509_lib.c
SRCS+= x509_lu.c
SRCS+= x509_ncons.c
SRCS+= x509_obj.c
SRCS+= x509_ocsp.c
SRCS+= x509_pcons.c
SRCS+= x509_pku.c
SRCS+= x509_pmaps.c
SRCS+= x509_policy.c
SRCS+= x509_prn.c
SRCS+= x509_purp.c
SRCS+= x509_r2x.c
SRCS+= x509_req.c
SRCS+= x509_set.c
SRCS+= x509_siginfo.c
SRCS+= x509_skey.c
SRCS+= x509_trs.c
SRCS+= x509_txt.c
SRCS+= x509_utl.c
SRCS+= x509_v3.c
SRCS+= x509_verify.c
SRCS+= x509_vfy.c
SRCS+= x509_vpm.c
SRCS+= x509cset.c
SRCS+= x509name.c
SRCS+= x509rset.c
SRCS+= x509spki.c
SRCS+= x509type.c
SRCS+= x_all.c

.PATH:	${LCRYPTO_SRC}/arch/${MACHINE_CPU} \
	${LCRYPTO_SRC} \
	${LCRYPTO_SRC}/aes \
	${LCRYPTO_SRC}/asn1 \
	${LCRYPTO_SRC}/bf \
	${LCRYPTO_SRC}/bio \
	${LCRYPTO_SRC}/bn \
	${LCRYPTO_SRC}/bn/arch/${MACHINE_CPU} \
	${LCRYPTO_SRC}/bn/asm \
	${LCRYPTO_SRC}/buffer \
	${LCRYPTO_SRC}/bytestring \
	${LCRYPTO_SRC}/camellia \
	${LCRYPTO_SRC}/cast \
	${LCRYPTO_SRC}/chacha \
	${LCRYPTO_SRC}/cmac \
	${LCRYPTO_SRC}/cms \
	${LCRYPTO_SRC}/comp \
	${LCRYPTO_SRC}/conf \
	${LCRYPTO_SRC}/ct \
	${LCRYPTO_SRC}/curve25519 \
	${LCRYPTO_SRC}/des \
	${LCRYPTO_SRC}/dh \
	${LCRYPTO_SRC}/dsa \
	${LCRYPTO_SRC}/ec \
	${LCRYPTO_SRC}/ecdh \
	${LCRYPTO_SRC}/ecdsa \
	${LCRYPTO_SRC}/engine \
	${LCRYPTO_SRC}/err \
	${LCRYPTO_SRC}/evp \
	${LCRYPTO_SRC}/hkdf \
	${LCRYPTO_SRC}/hmac \
	${LCRYPTO_SRC}/idea \
	${LCRYPTO_SRC}/kdf \
	${LCRYPTO_SRC}/lhash \
	${LCRYPTO_SRC}/md4 \
	${LCRYPTO_SRC}/md5 \
	${LCRYPTO_SRC}/mlkem \
	${LCRYPTO_SRC}/modes \
	${LCRYPTO_SRC}/objects \
	${LCRYPTO_SRC}/ocsp \
	${LCRYPTO_SRC}/pem \
	${LCRYPTO_SRC}/perlasm \
	${LCRYPTO_SRC}/pkcs12 \
	${LCRYPTO_SRC}/pkcs7 \
	${LCRYPTO_SRC}/poly1305 \
	${LCRYPTO_SRC}/rand \
	${LCRYPTO_SRC}/rc2 \
	${LCRYPTO_SRC}/rc4 \
	${LCRYPTO_SRC}/ripemd \
	${LCRYPTO_SRC}/rsa \
	${LCRYPTO_SRC}/sha \
	${LCRYPTO_SRC}/sm3 \
	${LCRYPTO_SRC}/sm4 \
	${LCRYPTO_SRC}/stack \
	${LCRYPTO_SRC}/threads \
	${LCRYPTO_SRC}/ts \
	${LCRYPTO_SRC}/txt_db \
	${LCRYPTO_SRC}/ui \
	${LCRYPTO_SRC}/x509

HDRS=\
	${LCRYPTO_SRC}/aes/aes.h \
	${LCRYPTO_SRC}/asn1/asn1.h \
	${LCRYPTO_SRC}/asn1/asn1t.h \
	${LCRYPTO_SRC}/asn1/posix_time.h \
	${LCRYPTO_SRC}/bf/blowfish.h \
	${LCRYPTO_SRC}/bio/bio.h \
	${LCRYPTO_SRC}/bn/bn.h \
	${LCRYPTO_SRC}/buffer/buffer.h \
	${LCRYPTO_SRC}/camellia/camellia.h \
	${LCRYPTO_SRC}/cast/cast.h \
	${LCRYPTO_SRC}/chacha/chacha.h \
	${LCRYPTO_SRC}/cmac/cmac.h \
	${LCRYPTO_SRC}/cms/cms.h \
	${LCRYPTO_SRC}/comp/comp.h \
	${LCRYPTO_SRC}/conf/conf.h \
	${LCRYPTO_SRC}/crypto.h \
	${LCRYPTO_SRC}/ct/ct.h \
	${LCRYPTO_SRC}/curve25519/curve25519.h \
	${LCRYPTO_SRC}/des/des.h \
	${LCRYPTO_SRC}/dh/dh.h \
	${LCRYPTO_SRC}/dsa/dsa.h \
	${LCRYPTO_SRC}/ec/ec.h \
	${LCRYPTO_SRC}/ecdh/ecdh.h \
	${LCRYPTO_SRC}/ecdsa/ecdsa.h \
	${LCRYPTO_SRC}/engine/engine.h \
	${LCRYPTO_SRC}/err/err.h \
	${LCRYPTO_SRC}/evp/evp.h \
	${LCRYPTO_SRC}/hkdf/hkdf.h \
	${LCRYPTO_SRC}/hmac/hmac.h \
	${LCRYPTO_SRC}/idea/idea.h \
	${LCRYPTO_SRC}/kdf/kdf.h \
	${LCRYPTO_SRC}/lhash/lhash.h \
	${LCRYPTO_SRC}/md4/md4.h \
	${LCRYPTO_SRC}/md5/md5.h \
	${LCRYPTO_SRC}/mlkem/mlkem.h \
	${LCRYPTO_SRC}/modes/modes.h \
	${LCRYPTO_SRC}/objects/objects.h \
	${LCRYPTO_SRC}/ocsp/ocsp.h \
	${LCRYPTO_SRC}/opensslconf.h \
	${LCRYPTO_SRC}/opensslfeatures.h \
	${LCRYPTO_SRC}/opensslv.h \
	${LCRYPTO_SRC}/ossl_typ.h \
	${LCRYPTO_SRC}/pem/pem.h \
	${LCRYPTO_SRC}/pkcs12/pkcs12.h \
	${LCRYPTO_SRC}/pkcs7/pkcs7.h \
	${LCRYPTO_SRC}/poly1305/poly1305.h \
	${LCRYPTO_SRC}/rand/rand.h \
	${LCRYPTO_SRC}/rc2/rc2.h \
	${LCRYPTO_SRC}/rc4/rc4.h \
	${LCRYPTO_SRC}/ripemd/ripemd.h \
	${LCRYPTO_SRC}/rsa/rsa.h \
	${LCRYPTO_SRC}/sha/sha.h \
	${LCRYPTO_SRC}/sm3/sm3.h \
	${LCRYPTO_SRC}/sm4/sm4.h \
	${LCRYPTO_SRC}/stack/safestack.h \
	${LCRYPTO_SRC}/stack/stack.h \
	${LCRYPTO_SRC}/ts/ts.h \
	${LCRYPTO_SRC}/txt_db/txt_db.h \
	${LCRYPTO_SRC}/ui/ui.h \
	${LCRYPTO_SRC}/x509/x509.h \
	${LCRYPTO_SRC}/x509/x509_vfy.h \
	${LCRYPTO_SRC}/x509/x509v3.h

HDRS_GEN=\
	${.OBJDIR}/obj_mac.h

prereq: obj_mac.h

includes: prereq
	@test -d ${DESTDIR}/usr/include/openssl || \
	    mkdir ${DESTDIR}/usr/include/openssl
	@for i in $(HDRS) $(HDRS_GEN); do \
	    j="cmp -s $$i ${DESTDIR}/usr/include/openssl/`basename $$i` || \
	    ${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 $$i\
		${DESTDIR}/usr/include/openssl"; \
	    echo $$j; \
	    eval "$$j"; \
	done;
${VERSION_SCRIPT}: ${SYMBOL_LIST}
	{ printf '{\n\tglobal:\n'; \
	  sed '/^[._a-zA-Z]/s/$$/;/; s/^/		_libre_/' ${SYMBOL_LIST}; \
	  sed '/^[._a-zA-Z]/s/$$/;/; s/^/		/' ${SYMBOL_LIST}; \
	  printf '\n\tlocal:\n\t\t*;\n};\n'; } >$@.tmp && mv $@.tmp $@

# generated
CFLAGS+= -I${.OBJDIR}

GENERATED=obj_mac.h obj_dat.h
CLEANFILES+=${GENERATED} obj_mac.num.tmp
SSL_OBJECTS=${LCRYPTO_SRC}/objects

obj_mac.h: ${SSL_OBJECTS}/objects.h ${SSL_OBJECTS}/obj_mac.num ${SSL_OBJECTS}/objects.txt ${SSL_OBJECTS}/objects.pl
	cat ${SSL_OBJECTS}/obj_mac.num > obj_mac.num.tmp
	/usr/bin/perl ${SSL_OBJECTS}/objects.pl ${SSL_OBJECTS}/objects.txt obj_mac.num.tmp obj_mac.h

obj_dat.h: obj_mac.h ${SSL_OBJECTS}/obj_dat.pl
	/usr/bin/perl ${SSL_OBJECTS}/obj_dat.pl obj_mac.h obj_dat.h

.if exists (${.CURDIR}/arch/${MACHINE_CPU}/Makefile.inc)
.include "${.CURDIR}/arch/${MACHINE_CPU}/Makefile.inc"
.else
CFLAGS+=-DOPENSSL_NO_ASM
.endif

BUILDFIRST = ${GENERATED}

.include <bsd.lib.mk>


distribution:
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	   ${.CURDIR}/openssl.cnf ${DESTDIR}/etc/ssl/openssl.cnf && \
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	   ${.CURDIR}/cert.pem ${DESTDIR}/etc/ssl/cert.pem && \
	${INSTALL} ${INSTALL_COPY} -o ${BINOWN} -g ${BINGRP} -m 444 \
	   ${.CURDIR}/x509v3.cnf ${DESTDIR}/etc/ssl/x509v3.cnf

all: ${PC_FILES}
${PC_FILES}: opensslv.h
	/bin/sh ${.CURDIR}/generate_pkgconfig.sh -c ${.CURDIR} -o ${.OBJDIR}

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.OBJDIR}/${PC_FILES} ${DESTDIR}/usr/lib/pkgconfig/

.include <bsd.subdir.mk>
