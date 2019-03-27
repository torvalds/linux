# $FreeBSD$

SHLIBDIR?=	/lib
SUBDIR=		engines

.include <bsd.own.mk>

LIB=		crypto
SHLIB_MAJOR=	111
VERSION_MAP=	${.CURDIR}/Version.map

NO_LINT=

.if exists(Makefile.man)
.include "Makefile.man"
.endif
.include "Makefile.inc"

SRCS=	cpt_err.c cryptlib.c ctype.c cversion.c ex_data.c getenv.c init.c
SRCS+=	mem.c mem_dbg.c mem_sec.c o_dir.c o_fips.c o_fopen.c o_init.c
SRCS+=	o_str.c o_time.c threads_pthread.c uid.c
.if defined(ASM_aarch64)
SRCS+=	arm64cpuid.S armcap.c
ACFLAGS.arm64cpuid.S=	-march=armv8-a+crypto
.elif defined(ASM_amd64)
SRCS+=	x86_64cpuid.S
.elif defined(ASM_arm)
SRCS+=	armv4cpuid.S armcap.c
.elif defined(ASM_i386)
SRCS+=	x86cpuid.S
.else
SRCS+=	mem_clr.c
.endif

# aes
SRCS+=	aes_cfb.c aes_ecb.c aes_ige.c aes_misc.c aes_ofb.c aes_wrap.c
.if defined(ASM_aarch64)
SRCS+=	aes_cbc.c aes_core.c aesv8-armx.S vpaes-armv8.S
ACFLAGS.aesv8-armx.S=	-march=armv8-a+crypto
.elif defined(ASM_amd64)
SRCS+=	aes-x86_64.S aesni-mb-x86_64.S aesni-sha1-x86_64.S
SRCS+=	aesni-sha256-x86_64.S aesni-x86_64.S bsaes-x86_64.S vpaes-x86_64.S
.elif defined(ASM_arm)
SRCS+=	aes-armv4.S aes_cbc.c aesv8-armx.S bsaes-armv7.S
.elif defined(ASM_i386)
SRCS+=	aes-586.S aesni-x86.S vpaes-x86.S
.else
SRCS+=	aes_cbc.c aes_core.c
.endif

# aria
SRCS+=	aria.c

# asn1
SRCS+=	a_bitstr.c a_d2i_fp.c a_digest.c a_dup.c a_gentm.c a_i2d_fp.c
SRCS+=	a_int.c a_mbstr.c a_object.c a_octet.c a_print.c a_sign.c a_strex.c
SRCS+=	a_strnid.c a_time.c a_type.c a_utctm.c a_utf8.c a_verify.c
SRCS+=	ameth_lib.c asn1_err.c asn1_gen.c asn1_item_list.c asn1_lib.c
SRCS+=	asn1_par.c asn_mime.c asn_moid.c asn_mstbl.c asn_pack.c bio_asn1.c
SRCS+=	bio_ndef.c d2i_pr.c d2i_pu.c evp_asn1.c f_int.c f_string.c i2d_pr.c
SRCS+=	i2d_pu.c n_pkey.c nsseq.c p5_pbe.c p5_pbev2.c p5_scrypt.c p8_pkey.c
SRCS+=	t_bitst.c t_pkey.c t_spki.c tasn_dec.c tasn_enc.c tasn_fre.c
SRCS+=	tasn_new.c tasn_prn.c tasn_scn.c tasn_typ.c tasn_utl.c x_algor.c
SRCS+=	x_bignum.c x_info.c x_int64.c x_long.c x_pkey.c x_sig.c x_spki.c
SRCS+=	x_val.c

# async
SRCS+=	async.c async_err.c async_posix.c async_wait.c

# bf
SRCS+=	bf_cfb64.c bf_ecb.c bf_ofb64.c bf_skey.c
.if defined(ASM_i386)
SRCS+=	bf-586.S
.else
SRCS+=	bf_enc.c
.endif

# bio
SRCS+=	b_addr.c b_dump.c b_print.c b_sock.c b_sock2.c bf_buff.c bf_lbuf.c
SRCS+=	bf_nbio.c bf_null.c bio_cb.c bio_err.c bio_lib.c bio_meth.c
SRCS+=	bss_acpt.c bss_bio.c bss_conn.c bss_dgram.c bss_fd.c bss_file.c
SRCS+=	bss_log.c bss_mem.c bss_null.c bss_sock.c

# blake2
SRCS+=	blake2b.c blake2s.c m_blake2b.c m_blake2s.c

# bn
SRCS+=	bn_add.c bn_blind.c bn_const.c bn_ctx.c bn_depr.c bn_dh.c bn_div.c
SRCS+=	bn_err.c bn_exp.c bn_exp2.c bn_gcd.c bn_gf2m.c bn_intern.c bn_kron.c
SRCS+=	bn_lib.c bn_mod.c bn_mont.c bn_mpi.c bn_mul.c bn_nist.c bn_prime.c
SRCS+=	bn_print.c bn_rand.c bn_recp.c bn_shift.c bn_sqr.c bn_sqrt.c
SRCS+=	bn_srp.c bn_word.c bn_x931p.c
.if defined(ASM_aarch64)
SRCS+=	armv8-mont.S bn_asm.c
.elif defined(ASM_amd64)
SRCS+=	rsaz-avx2.S rsaz-x86_64.S rsaz_exp.c x86_64-gcc.c x86_64-gf2m.S
SRCS+=	x86_64-mont.S x86_64-mont5.S
.elif defined(ASM_arm)
SRCS+=	armv4-gf2m.S armv4-mont.S bn_asm.c
.elif defined(ASM_i386)
SRCS+=	bn-586.S co-586.S x86-gf2m.S x86-mont.S
.else
SRCS+=	bn_asm.c
.endif

# buffer
SRCS+=	buf_err.c buffer.c

# camellia
SRCS+=	cmll_cfb.c cmll_ctr.c cmll_ecb.c cmll_ofb.c
.if defined(ASM_amd64)
SRCS+=	cmll-x86_64.S cmll_misc.c
.elif defined(ASM_i386)
SRCS+=	cmll-x86.S
.else
SRCS+=	camellia.c cmll_cbc.c cmll_misc.c
.endif

# cast
SRCS+=	c_cfb64.c c_ecb.c c_enc.c c_ofb64.c c_skey.c

# chacha
.if defined(ASM_aarch64)
SRCS+=	chacha-armv8.S
.elif defined(ASM_amd64)
SRCS+=	chacha-x86_64.S
.elif defined(ASM_arm)
SRCS+=	chacha-armv4.S
.elif defined(ASM_i386)
SRCS+=	chacha-x86.S
.else
SRCS+=	chacha_enc.c
.endif

# cmac
SRCS+=	cm_ameth.c cm_pmeth.c cmac.c

# cms
SRCS+=	cms_asn1.c cms_att.c cms_dd.c cms_enc.c cms_env.c cms_err.c
SRCS+=	cms_ess.c cms_io.c cms_kari.c cms_lib.c cms_pwri.c cms_sd.c
SRCS+=	cms_smime.c

# comp
SRCS+=	c_zlib.c comp_err.c comp_lib.c

# conf
SRCS+=	conf_api.c conf_def.c conf_err.c conf_lib.c conf_mall.c conf_mod.c
SRCS+=	conf_sap.c conf_ssl.c

# ct
SRCS+=	ct_b64.c ct_err.c ct_log.c ct_oct.c ct_policy.c ct_prn.c ct_sct.c
SRCS+=	ct_sct_ctx.c ct_vfy.c ct_x509v3.c

# des
SRCS+=	cbc_cksm.c cbc_enc.c cfb64ede.c cfb64enc.c cfb_enc.c ecb3_enc.c
SRCS+=	ecb_enc.c fcrypt.c ofb64ede.c ofb64enc.c ofb_enc.c pcbc_enc.c
SRCS+=	qud_cksm.c rand_key.c set_key.c str2key.c xcbc_enc.c
.if defined(ASM_i386)
SRCS+=	crypt586.S des-586.S
.else
SRCS+=	des_enc.c fcrypt_b.c
.endif

# dh
SRCS+=	dh_ameth.c dh_asn1.c dh_check.c dh_depr.c dh_err.c dh_gen.c dh_kdf.c
SRCS+=	dh_key.c dh_lib.c dh_meth.c dh_pmeth.c dh_prn.c dh_rfc5114.c
SRCS+=	dh_rfc7919.c

# dsa
SRCS+=	dsa_ameth.c dsa_asn1.c dsa_depr.c dsa_err.c dsa_gen.c dsa_key.c
SRCS+=	dsa_lib.c dsa_meth.c dsa_ossl.c dsa_pmeth.c dsa_prn.c dsa_sign.c
SRCS+=	dsa_vrf.c

# dso
SRCS+=	dso_dlfcn.c dso_err.c dso_lib.c

# ec
SRCS+=	curve25519.c curve448.c curve448_tables.c ec2_oct.c ec2_smpl.c
SRCS+=	ec_ameth.c ec_asn1.c ec_check.c ec_curve.c ec_cvt.c ec_err.c
SRCS+=	ec_key.c ec_kmeth.c ec_lib.c ec_mult.c ec_oct.c ec_pmeth.c
SRCS+=	ec_print.c ecdh_kdf.c ecdh_ossl.c ecdsa_ossl.c ecdsa_sign.c
SRCS+=	ecdsa_vrf.c eck_prn.c ecp_mont.c ecp_nist.c ecp_oct.c ecp_smpl.c
SRCS+=	ecx_meth.c eddsa.c f_generic.c f_impl.c scalar.c
.if defined(ASM_aarch64)
SRCS+=	ecp_nistz256-armv8.S ecp_nistz256.c
.elif defined(ASM_amd64)
SRCS+=	ecp_nistz256-x86_64.S ecp_nistz256.c x25519-x86_64.S
.elif defined(ASM_arm)
SRCS+=	ecp_nistz256-armv4.S ecp_nistz256.c
.elif defined(ASM_i386)
SRCS+=	ecp_nistz256-x86.S ecp_nistz256.c
.endif

# engine
SRCS+=	eng_all.c eng_cnf.c eng_ctrl.c eng_devcrypto.c eng_dyn.c eng_err.c
SRCS+=	eng_fat.c eng_init.c eng_lib.c eng_list.c eng_openssl.c eng_pkey.c
SRCS+=	eng_rdrand.c eng_table.c tb_asnmth.c tb_cipher.c tb_dh.c tb_digest.c
SRCS+=	tb_dsa.c tb_eckey.c tb_pkmeth.c tb_rand.c tb_rsa.c

# err
SRCS+=	err.c err_all.c err_prn.c

# evp
SRCS+=	bio_b64.c bio_enc.c bio_md.c bio_ok.c c_allc.c c_alld.c cmeth_lib.c
SRCS+=	digest.c e_aes.c e_aes_cbc_hmac_sha1.c e_aes_cbc_hmac_sha256.c
SRCS+=	e_aria.c e_bf.c e_camellia.c e_cast.c e_chacha20_poly1305.c e_des.c
SRCS+=	e_des3.c e_idea.c e_null.c e_old.c e_rc2.c e_rc4.c e_rc4_hmac_md5.c
SRCS+=	e_rc5.c e_seed.c e_sm4.c e_xcbc_d.c encode.c evp_cnf.c evp_enc.c
SRCS+=	evp_err.c evp_key.c evp_lib.c evp_pbe.c evp_pkey.c m_md4.c m_md5.c
SRCS+=	m_md5_sha1.c m_mdc2.c m_null.c m_ripemd.c m_sha1.c m_sha3.c
SRCS+=	m_sigver.c m_wp.c names.c p5_crpt.c p5_crpt2.c p_dec.c p_enc.c
SRCS+=	p_lib.c p_open.c p_seal.c p_sign.c p_verify.c pbe_scrypt.c
SRCS+=	pmeth_fn.c pmeth_gn.c pmeth_lib.c

# hmac
SRCS+=	hm_ameth.c hm_pmeth.c hmac.c

# idea
SRCS+=	i_cbc.c i_cfb64.c i_ecb.c i_ofb64.c i_skey.c

# kdf
SRCS+=	hkdf.c kdf_err.c scrypt.c tls1_prf.c

# lhash
SRCS+=	lh_stats.c lhash.c

# md4
SRCS+=	md4_dgst.c md4_one.c

# md5
SRCS+=	md5_dgst.c md5_one.c
.if defined(ASM_amd64)
SRCS+=	md5-x86_64.S
.elif defined(ASM_i386)
SRCS+=	md5-586.S
.endif

# mdc2
SRCS+=	mdc2_one.c mdc2dgst.c

# modes
SRCS+=	cbc128.c ccm128.c cfb128.c ctr128.c cts128.c gcm128.c ocb128.c
SRCS+=	ofb128.c wrap128.c xts128.c
.if defined(ASM_aarch64)
SRCS+=	ghashv8-armx.S
ACFLAGS.ghashv8-armx.S=	-march=armv8-a+crypto
.elif defined(ASM_amd64)
SRCS+=	aesni-gcm-x86_64.S ghash-x86_64.S
.elif defined(ASM_arm)
SRCS+=	ghash-armv4.S ghashv8-armx.S
.elif defined(ASM_i386)
SRCS+=	ghash-x86.S
.endif

# objects
SRCS+=	o_names.c obj_dat.c obj_err.c obj_lib.c obj_xref.c

# ocsp
SRCS+=	ocsp_asn.c ocsp_cl.c ocsp_err.c ocsp_ext.c ocsp_ht.c ocsp_lib.c
SRCS+=	ocsp_prn.c ocsp_srv.c ocsp_vfy.c v3_ocsp.c

# pem
SRCS+=	pem_all.c pem_err.c pem_info.c pem_lib.c pem_oth.c pem_pk8.c
SRCS+=	pem_pkey.c pem_sign.c pem_x509.c pem_xaux.c pvkfmt.c

# pkcs12
SRCS+=	p12_add.c p12_asn.c p12_attr.c p12_crpt.c p12_crt.c p12_decr.c
SRCS+=	p12_init.c p12_key.c p12_kiss.c p12_mutl.c p12_npas.c p12_p8d.c
SRCS+=	p12_p8e.c p12_sbag.c p12_utl.c pk12err.c

# pkcs7
SRCS+=	bio_pk7.c pk7_asn1.c pk7_attr.c pk7_doit.c pk7_lib.c pk7_mime.c
SRCS+=	pk7_smime.c pkcs7err.c

# poly1305
SRCS+=	poly1305.c poly1305_ameth.c poly1305_pmeth.c
.if defined(ASM_aarch64)
SRCS+=	poly1305-armv8.S
.elif defined(ASM_amd64)
SRCS+=	poly1305-x86_64.S
.elif defined(ASM_arm)
SRCS+=	poly1305-armv4.S
.elif defined(ASM_i386)
SRCS+=	poly1305-x86.S
.endif

# rand
SRCS+=	drbg_ctr.c drbg_lib.c rand_err.c rand_lib.c rand_unix.c randfile.c

# rc2
SRCS+=	rc2_cbc.c rc2_ecb.c rc2_skey.c rc2cfb64.c rc2ofb64.c

# rc4
.if defined(ASM_amd64)
SRCS+=	rc4-md5-x86_64.S rc4-x86_64.S
.elif defined(ASM_i386)
SRCS+=	rc4-586.S
.else
SRCS+=	rc4_enc.c rc4_skey.c
.endif

# rc5
SRCS+=	rc5_ecb.c rc5_skey.c rc5cfb64.c rc5ofb64.c
.if defined(ASM_i386)
SRCS+=	rc5-586.S
.else
SRCS+=	rc5_enc.c
.endif

# ripemd
SRCS+=	rmd_dgst.c rmd_one.c
.if defined(ASM_i386)
SRCS+=	rmd-586.S
.endif

# rsa
SRCS+=	rsa_ameth.c rsa_asn1.c rsa_chk.c rsa_crpt.c rsa_depr.c rsa_err.c
SRCS+=	rsa_gen.c rsa_lib.c rsa_meth.c rsa_mp.c rsa_none.c rsa_oaep.c
SRCS+=	rsa_ossl.c rsa_pk1.c rsa_pmeth.c rsa_prn.c rsa_pss.c rsa_saos.c
SRCS+=	rsa_sign.c rsa_ssl.c rsa_x931.c rsa_x931g.c

# seed
SRCS+=	seed.c seed_cbc.c seed_cfb.c seed_ecb.c seed_ofb.c

# sha
SRCS+=	sha1_one.c sha1dgst.c sha256.c sha512.c
.if defined(ASM_aarch64)
SRCS+=	keccak1600-armv8.S sha1-armv8.S sha256-armv8.S sha512-armv8.S
.elif defined(ASM_amd64)
SRCS+=	keccak1600-x86_64.S sha1-mb-x86_64.S sha1-x86_64.S
SRCS+=	sha256-mb-x86_64.S sha256-x86_64.S sha512-x86_64.S
.elif defined(ASM_arm)
SRCS+=	keccak1600-armv4.S sha1-armv4-large.S sha256-armv4.S sha512-armv4.S
.elif defined(ASM_i386)
SRCS+=	keccak1600.c sha1-586.S sha256-586.S sha512-586.S
.else
SRCS+=	keccak1600.c
.endif

# siphash
SRCS+=	siphash.c siphash_ameth.c siphash_pmeth.c

# sm2
SRCS+=	sm2_crypt.c sm2_err.c sm2_pmeth.c sm2_sign.c

# sm3
SRCS+=	m_sm3.c sm3.c

# sm4
SRCS+=	sm4.c

# srp
SRCS+=	srp_lib.c srp_vfy.c

# stack
SRCS+=	stack.c

# store
SRCS+=	loader_file.c store_err.c store_init.c store_lib.c store_register.c
SRCS+=	store_strings.c

# ts
SRCS+=	ts_asn1.c ts_conf.c ts_err.c ts_lib.c ts_req_print.c ts_req_utils.c
SRCS+=	ts_rsp_print.c ts_rsp_sign.c ts_rsp_utils.c ts_rsp_verify.c
SRCS+=	ts_verify_ctx.c

# txt_db
SRCS+=	txt_db.c

# ui
SRCS+=	ui_err.c ui_lib.c ui_null.c ui_openssl.c ui_util.c

# whrlpool
SRCS+=	wp_dgst.c
.if defined(ASM_amd64)
SRCS+=	wp-x86_64.S
.elif defined(ASM_i386)
SRCS+=	wp-mmx.S wp_block.c
.else
SRCS+=	wp_block.c
.endif

# x509
SRCS+=	by_dir.c by_file.c t_crl.c t_req.c t_x509.c x509_att.c x509_cmp.c
SRCS+=	x509_d2.c x509_def.c x509_err.c x509_ext.c x509_lu.c x509_meth.c
SRCS+=	x509_obj.c x509_r2x.c x509_req.c x509_set.c x509_trs.c x509_txt.c
SRCS+=	x509_v3.c x509_vfy.c x509_vpm.c x509cset.c x509name.c x509rset.c
SRCS+=	x509spki.c x509type.c x_all.c x_attrib.c x_crl.c x_exten.c x_name.c
SRCS+=	x_pubkey.c x_req.c x_x509.c x_x509a.c

# x509v3
SRCS+=	pcy_cache.c pcy_data.c pcy_lib.c pcy_map.c pcy_node.c pcy_tree.c
SRCS+=	v3_addr.c v3_admis.c v3_akey.c v3_akeya.c v3_alt.c v3_asid.c
SRCS+=	v3_bcons.c v3_bitst.c v3_conf.c v3_cpols.c v3_crld.c v3_enum.c
SRCS+=	v3_extku.c v3_genn.c v3_ia5.c v3_info.c v3_int.c v3_lib.c v3_ncons.c
SRCS+=	v3_pci.c v3_pcia.c v3_pcons.c v3_pku.c v3_pmaps.c v3_prn.c v3_purp.c
SRCS+=	v3_skey.c v3_sxnet.c v3_tlsf.c v3_utl.c v3err.c

INCS=	aes.h asn1.h asn1_mac.h asn1err.h asn1t.h async.h asyncerr.h bio.h
INCS+=	bioerr.h blowfish.h bn.h bnerr.h buffer.h buffererr.h camellia.h
INCS+=	cast.h cmac.h cms.h cmserr.h comp.h comperr.h conf.h conf_api.h
INCS+=	conferr.h crypto.h cryptoerr.h ct.h cterr.h des.h dh.h dherr.h dsa.h
INCS+=	dsaerr.h dtls1.h e_os2.h ebcdic.h ec.h ecdh.h ecdsa.h ecerr.h
INCS+=	engine.h engineerr.h err.h evp.h evperr.h hmac.h idea.h kdf.h
INCS+=	kdferr.h lhash.h md2.h md4.h md5.h mdc2.h modes.h obj_mac.h
INCS+=	objects.h objectserr.h ocsp.h ocsperr.h opensslconf.h opensslv.h
INCS+=	ossl_typ.h pem.h pem2.h pemerr.h pkcs12.h pkcs12err.h pkcs7.h
INCS+=	pkcs7err.h rand.h rand_drbg.h randerr.h rc2.h rc4.h rc5.h ripemd.h
INCS+=	rsa.h rsaerr.h safestack.h seed.h sha.h srp.h srtp.h ssl.h ssl2.h
INCS+=	ssl3.h sslerr.h stack.h store.h storeerr.h symhacks.h tls1.h ts.h
INCS+=	tserr.h txt_db.h ui.h uierr.h whrlpool.h x509.h x509_vfy.h x509err.h
INCS+=	x509v3.h x509v3err.h

INCSDIR=	${INCLUDEDIR}/openssl

LIBADD=	pthread

CFLAGS+=	-I${LCRYPTO_SRC}/crypto
CFLAGS+=	-I${LCRYPTO_SRC}/crypto/ec/curve448
CFLAGS+=	-I${LCRYPTO_SRC}/crypto/ec/curve448/arch_32
CFLAGS+=	-I${LCRYPTO_SRC}/crypto/modes
CFLAGS+=	-I${.OBJDIR}

.if !empty(SRCS:M*.S)
ACFLAGS+=	-Wa,--noexecstack
.endif

SRCS+=	buildinf.h

CLEANDIRS=	openssl
CLEANFILES=	buildinf.h opensslconf.h opensslconf.h.tmp

buildinf.h: Makefile
	( echo "/*"; \
	echo " * WARNING: do not edit!"; \
	echo " * Generated by ${.ALLSRC}"; \
	echo " */"; \
	echo "#define PLATFORM \"platform: FreeBSD-${MACHINE_ARCH}\""; \
	echo "#define DATE \"built on: reproducible build, date unspecified\""; \
	echo "static const char compiler_flags[] = \"compiler: ${COMPILER_TYPE}\";" ) \
	> ${.TARGET}

beforedepend: openssl_opensslconf.h
beforebuild: openssl_opensslconf.h

openssl_opensslconf.h: opensslconf.h
	mkdir -p openssl
	ln -fs ${.OBJDIR}/${.ALLSRC} openssl/${.ALLSRC}

opensslconf.h: opensslconf.h.in
.if defined(ASM_${MACHINE_CPUARCH})
	sed 's/%%ASM%%//; /%%NO_ASM%%/d' ${.ALLSRC} > ${.TARGET}.tmp
.else
	sed '/%%ASM%%/d; s/%%NO_ASM%%//' ${.ALLSRC} > ${.TARGET}.tmp
.endif
	mv -f ${.TARGET}.tmp ${.TARGET}

.include <bsd.lib.mk>

PICFLAG+=	-DOPENSSL_PIC

.if defined(ASM_${MACHINE_CPUARCH})
.PATH:	${.CURDIR}/${MACHINE_CPUARCH}
.if defined(ASM_amd64)
.PATH:	${LCRYPTO_SRC}/crypto/bn/asm
.endif
.endif

.PATH:	${LCRYPTO_SRC}/crypto \
	${LCRYPTO_SRC}/crypto/aes \
	${LCRYPTO_SRC}/crypto/aria \
	${LCRYPTO_SRC}/crypto/asn1 \
	${LCRYPTO_SRC}/crypto/async \
	${LCRYPTO_SRC}/crypto/async/arch \
	${LCRYPTO_SRC}/crypto/bf \
	${LCRYPTO_SRC}/crypto/bio \
	${LCRYPTO_SRC}/crypto/blake2 \
	${LCRYPTO_SRC}/crypto/bn \
	${LCRYPTO_SRC}/crypto/buffer \
	${LCRYPTO_SRC}/crypto/camellia \
	${LCRYPTO_SRC}/crypto/cast \
	${LCRYPTO_SRC}/crypto/chacha \
	${LCRYPTO_SRC}/crypto/cmac \
	${LCRYPTO_SRC}/crypto/cms \
	${LCRYPTO_SRC}/crypto/comp \
	${LCRYPTO_SRC}/crypto/conf \
	${LCRYPTO_SRC}/crypto/ct \
	${LCRYPTO_SRC}/crypto/des \
	${LCRYPTO_SRC}/crypto/dh \
	${LCRYPTO_SRC}/crypto/dsa \
	${LCRYPTO_SRC}/crypto/dso \
	${LCRYPTO_SRC}/crypto/ec \
	${LCRYPTO_SRC}/crypto/ec/curve448 \
	${LCRYPTO_SRC}/crypto/ec/curve448/arch_32 \
	${LCRYPTO_SRC}/crypto/engine \
	${LCRYPTO_SRC}/crypto/err \
	${LCRYPTO_SRC}/crypto/evp \
	${LCRYPTO_SRC}/crypto/hmac \
	${LCRYPTO_SRC}/crypto/idea \
	${LCRYPTO_SRC}/crypto/kdf \
	${LCRYPTO_SRC}/crypto/lhash \
	${LCRYPTO_SRC}/crypto/md4 \
	${LCRYPTO_SRC}/crypto/md5 \
	${LCRYPTO_SRC}/crypto/mdc2 \
	${LCRYPTO_SRC}/crypto/modes \
	${LCRYPTO_SRC}/crypto/objects \
	${LCRYPTO_SRC}/crypto/ocsp \
	${LCRYPTO_SRC}/crypto/pem \
	${LCRYPTO_SRC}/crypto/pkcs12 \
	${LCRYPTO_SRC}/crypto/pkcs7 \
	${LCRYPTO_SRC}/crypto/poly1305 \
	${LCRYPTO_SRC}/crypto/rand \
	${LCRYPTO_SRC}/crypto/rc2 \
	${LCRYPTO_SRC}/crypto/rc4 \
	${LCRYPTO_SRC}/crypto/rc5 \
	${LCRYPTO_SRC}/crypto/ripemd \
	${LCRYPTO_SRC}/crypto/rsa \
	${LCRYPTO_SRC}/crypto/seed \
	${LCRYPTO_SRC}/crypto/sha \
	${LCRYPTO_SRC}/crypto/siphash \
	${LCRYPTO_SRC}/crypto/sm2 \
	${LCRYPTO_SRC}/crypto/sm3 \
	${LCRYPTO_SRC}/crypto/sm4 \
	${LCRYPTO_SRC}/crypto/srp \
	${LCRYPTO_SRC}/crypto/stack \
	${LCRYPTO_SRC}/crypto/store \
	${LCRYPTO_SRC}/crypto/ts \
	${LCRYPTO_SRC}/crypto/txt_db \
	${LCRYPTO_SRC}/crypto/ui \
	${LCRYPTO_SRC}/crypto/whrlpool \
	${LCRYPTO_SRC}/crypto/x509 \
	${LCRYPTO_SRC}/crypto/x509v3 \
	${LCRYPTO_SRC}/include/openssl \
	${.CURDIR}/man
