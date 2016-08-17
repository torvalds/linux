/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_IOCTL_H
#define	_SYS_CRYPTO_IOCTL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/common.h>

#define	CRYPTO_MAX_ATTRIBUTE_COUNT	128

#define	CRYPTO_IOFLAGS_RW_SESSION	0x00000001

#define	CRYPTO(x)		(('y' << 8) | (x))

#define	MAX_NUM_THRESHOLD	7

/* the PKCS11 Mechanisms */
#define	CKM_RC4			0x00000111
#define	CKM_DES3_ECB		0x00000132
#define	CKM_DES3_CBC		0x00000133
#define	CKM_MD5			0x00000210
#define	CKM_SHA_1		0x00000220
#define	CKM_AES_ECB		0x00001081
#define	CKM_AES_CBC		0x00001082

/*
 * General Purpose Ioctls
 */

typedef struct fl_mechs_threshold {
	int		mech_type;
	uint32_t	mech_threshold;
} fl_mechs_threshold_t;

typedef struct crypto_function_list {
	boolean_t fl_digest_init;
	boolean_t fl_digest;
	boolean_t fl_digest_update;
	boolean_t fl_digest_key;
	boolean_t fl_digest_final;

	boolean_t fl_encrypt_init;
	boolean_t fl_encrypt;
	boolean_t fl_encrypt_update;
	boolean_t fl_encrypt_final;

	boolean_t fl_decrypt_init;
	boolean_t fl_decrypt;
	boolean_t fl_decrypt_update;
	boolean_t fl_decrypt_final;

	boolean_t fl_mac_init;
	boolean_t fl_mac;
	boolean_t fl_mac_update;
	boolean_t fl_mac_final;

	boolean_t fl_sign_init;
	boolean_t fl_sign;
	boolean_t fl_sign_update;
	boolean_t fl_sign_final;
	boolean_t fl_sign_recover_init;
	boolean_t fl_sign_recover;

	boolean_t fl_verify_init;
	boolean_t fl_verify;
	boolean_t fl_verify_update;
	boolean_t fl_verify_final;
	boolean_t fl_verify_recover_init;
	boolean_t fl_verify_recover;

	boolean_t fl_digest_encrypt_update;
	boolean_t fl_decrypt_digest_update;
	boolean_t fl_sign_encrypt_update;
	boolean_t fl_decrypt_verify_update;

	boolean_t fl_seed_random;
	boolean_t fl_generate_random;

	boolean_t fl_session_open;
	boolean_t fl_session_close;
	boolean_t fl_session_login;
	boolean_t fl_session_logout;

	boolean_t fl_object_create;
	boolean_t fl_object_copy;
	boolean_t fl_object_destroy;
	boolean_t fl_object_get_size;
	boolean_t fl_object_get_attribute_value;
	boolean_t fl_object_set_attribute_value;
	boolean_t fl_object_find_init;
	boolean_t fl_object_find;
	boolean_t fl_object_find_final;

	boolean_t fl_key_generate;
	boolean_t fl_key_generate_pair;
	boolean_t fl_key_wrap;
	boolean_t fl_key_unwrap;
	boolean_t fl_key_derive;

	boolean_t fl_init_token;
	boolean_t fl_init_pin;
	boolean_t fl_set_pin;

	boolean_t prov_is_limited;
	uint32_t prov_hash_threshold;
	uint32_t prov_hash_limit;

	int total_threshold_count;
	fl_mechs_threshold_t	fl_threshold[MAX_NUM_THRESHOLD];
} crypto_function_list_t;

typedef struct crypto_get_function_list {
	uint_t			fl_return_value;
	crypto_provider_id_t	fl_provider_id;
	crypto_function_list_t	fl_list;
} crypto_get_function_list_t;

typedef struct crypto_get_mechanism_number {
	uint_t			pn_return_value;
	caddr_t			pn_mechanism_string;
	size_t			pn_mechanism_len;
	crypto_mech_type_t	pn_internal_number;
} crypto_get_mechanism_number_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_get_mechanism_number32 {
	uint32_t		pn_return_value;
	caddr32_t		pn_mechanism_string;
	size32_t		pn_mechanism_len;
	crypto_mech_type_t	pn_internal_number;
} crypto_get_mechanism_number32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_GET_FUNCTION_LIST	CRYPTO(20)
#define	CRYPTO_GET_MECHANISM_NUMBER	CRYPTO(21)

/*
 * Session Ioctls
 */

typedef uint32_t	crypto_flags_t;

typedef struct crypto_open_session {
	uint_t			os_return_value;
	crypto_session_id_t	os_session;
	crypto_flags_t		os_flags;
	crypto_provider_id_t	os_provider_id;
} crypto_open_session_t;

typedef struct crypto_close_session {
	uint_t			cs_return_value;
	crypto_session_id_t	cs_session;
} crypto_close_session_t;

typedef struct crypto_close_all_sessions {
	uint_t			as_return_value;
	crypto_provider_id_t	as_provider_id;
} crypto_close_all_sessions_t;

#define	CRYPTO_OPEN_SESSION		CRYPTO(30)
#define	CRYPTO_CLOSE_SESSION		CRYPTO(31)
#define	CRYPTO_CLOSE_ALL_SESSIONS	CRYPTO(32)

/*
 * Login Ioctls
 */
typedef struct crypto_login {
	uint_t			co_return_value;
	crypto_session_id_t	co_session;
	uint_t			co_user_type;
	uint_t			co_pin_len;
	caddr_t			co_pin;
} crypto_login_t;

typedef struct crypto_logout {
	uint_t			cl_return_value;
	crypto_session_id_t	cl_session;
} crypto_logout_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_login32 {
	uint32_t		co_return_value;
	crypto_session_id_t	co_session;
	uint32_t		co_user_type;
	uint32_t		co_pin_len;
	caddr32_t		co_pin;
} crypto_login32_t;

typedef struct crypto_logout32 {
	uint32_t		cl_return_value;
	crypto_session_id_t	cl_session;
} crypto_logout32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_LOGIN			CRYPTO(40)
#define	CRYPTO_LOGOUT			CRYPTO(41)

/* flag for encrypt and decrypt operations */
#define	CRYPTO_INPLACE_OPERATION	0x00000001

/*
 * Cryptographic Ioctls
 */
typedef struct crypto_encrypt {
	uint_t			ce_return_value;
	crypto_session_id_t	ce_session;
	size_t			ce_datalen;
	caddr_t			ce_databuf;
	size_t			ce_encrlen;
	caddr_t			ce_encrbuf;
	uint_t			ce_flags;
} crypto_encrypt_t;

typedef struct crypto_encrypt_init {
	uint_t			ei_return_value;
	crypto_session_id_t	ei_session;
	crypto_mechanism_t	ei_mech;
	crypto_key_t		ei_key;
} crypto_encrypt_init_t;

typedef struct crypto_encrypt_update {
	uint_t			eu_return_value;
	crypto_session_id_t	eu_session;
	size_t			eu_datalen;
	caddr_t			eu_databuf;
	size_t			eu_encrlen;
	caddr_t			eu_encrbuf;
} crypto_encrypt_update_t;

typedef struct crypto_encrypt_final {
	uint_t			ef_return_value;
	crypto_session_id_t	ef_session;
	size_t			ef_encrlen;
	caddr_t			ef_encrbuf;
} crypto_encrypt_final_t;

typedef struct crypto_decrypt {
	uint_t			cd_return_value;
	crypto_session_id_t	cd_session;
	size_t			cd_encrlen;
	caddr_t			cd_encrbuf;
	size_t			cd_datalen;
	caddr_t			cd_databuf;
	uint_t			cd_flags;
} crypto_decrypt_t;

typedef struct crypto_decrypt_init {
	uint_t			di_return_value;
	crypto_session_id_t	di_session;
	crypto_mechanism_t	di_mech;
	crypto_key_t		di_key;
} crypto_decrypt_init_t;

typedef struct crypto_decrypt_update {
	uint_t			du_return_value;
	crypto_session_id_t	du_session;
	size_t			du_encrlen;
	caddr_t			du_encrbuf;
	size_t			du_datalen;
	caddr_t			du_databuf;
} crypto_decrypt_update_t;

typedef struct crypto_decrypt_final {
	uint_t			df_return_value;
	crypto_session_id_t	df_session;
	size_t			df_datalen;
	caddr_t			df_databuf;
} crypto_decrypt_final_t;

typedef struct crypto_digest {
	uint_t			cd_return_value;
	crypto_session_id_t	cd_session;
	size_t			cd_datalen;
	caddr_t			cd_databuf;
	size_t			cd_digestlen;
	caddr_t			cd_digestbuf;
} crypto_digest_t;

typedef struct crypto_digest_init {
	uint_t			di_return_value;
	crypto_session_id_t	di_session;
	crypto_mechanism_t	di_mech;
} crypto_digest_init_t;

typedef struct crypto_digest_update {
	uint_t			du_return_value;
	crypto_session_id_t	du_session;
	size_t			du_datalen;
	caddr_t			du_databuf;
} crypto_digest_update_t;

typedef struct crypto_digest_key {
	uint_t			dk_return_value;
	crypto_session_id_t	dk_session;
	crypto_key_t		dk_key;
} crypto_digest_key_t;

typedef struct crypto_digest_final {
	uint_t			df_return_value;
	crypto_session_id_t	df_session;
	size_t			df_digestlen;
	caddr_t			df_digestbuf;
} crypto_digest_final_t;

typedef struct crypto_mac {
	uint_t			cm_return_value;
	crypto_session_id_t	cm_session;
	size_t			cm_datalen;
	caddr_t			cm_databuf;
	size_t			cm_maclen;
	caddr_t			cm_macbuf;
} crypto_mac_t;

typedef struct crypto_mac_init {
	uint_t			mi_return_value;
	crypto_session_id_t	mi_session;
	crypto_mechanism_t	mi_mech;
	crypto_key_t		mi_key;
} crypto_mac_init_t;

typedef struct crypto_mac_update {
	uint_t			mu_return_value;
	crypto_session_id_t	mu_session;
	size_t			mu_datalen;
	caddr_t			mu_databuf;
} crypto_mac_update_t;

typedef struct crypto_mac_final {
	uint_t			mf_return_value;
	crypto_session_id_t	mf_session;
	size_t			mf_maclen;
	caddr_t			mf_macbuf;
} crypto_mac_final_t;

typedef struct crypto_sign {
	uint_t			cs_return_value;
	crypto_session_id_t	cs_session;
	size_t			cs_datalen;
	caddr_t			cs_databuf;
	size_t			cs_signlen;
	caddr_t			cs_signbuf;
} crypto_sign_t;

typedef struct crypto_sign_init {
	uint_t			si_return_value;
	crypto_session_id_t	si_session;
	crypto_mechanism_t	si_mech;
	crypto_key_t		si_key;
} crypto_sign_init_t;

typedef struct crypto_sign_update {
	uint_t			su_return_value;
	crypto_session_id_t	su_session;
	size_t			su_datalen;
	caddr_t			su_databuf;
} crypto_sign_update_t;

typedef struct crypto_sign_final {
	uint_t			sf_return_value;
	crypto_session_id_t	sf_session;
	size_t			sf_signlen;
	caddr_t			sf_signbuf;
} crypto_sign_final_t;

typedef struct crypto_sign_recover_init {
	uint_t			ri_return_value;
	crypto_session_id_t	ri_session;
	crypto_mechanism_t	ri_mech;
	crypto_key_t		ri_key;
} crypto_sign_recover_init_t;

typedef struct crypto_sign_recover {
	uint_t			sr_return_value;
	crypto_session_id_t	sr_session;
	size_t			sr_datalen;
	caddr_t			sr_databuf;
	size_t			sr_signlen;
	caddr_t			sr_signbuf;
} crypto_sign_recover_t;

typedef struct crypto_verify {
	uint_t			cv_return_value;
	crypto_session_id_t	cv_session;
	size_t			cv_datalen;
	caddr_t			cv_databuf;
	size_t			cv_signlen;
	caddr_t			cv_signbuf;
} crypto_verify_t;

typedef struct crypto_verify_init {
	uint_t			vi_return_value;
	crypto_session_id_t	vi_session;
	crypto_mechanism_t	vi_mech;
	crypto_key_t		vi_key;
} crypto_verify_init_t;

typedef struct crypto_verify_update {
	uint_t			vu_return_value;
	crypto_session_id_t	vu_session;
	size_t			vu_datalen;
	caddr_t			vu_databuf;
} crypto_verify_update_t;

typedef struct crypto_verify_final {
	uint_t			vf_return_value;
	crypto_session_id_t	vf_session;
	size_t			vf_signlen;
	caddr_t			vf_signbuf;
} crypto_verify_final_t;

typedef struct crypto_verify_recover_init {
	uint_t			ri_return_value;
	crypto_session_id_t	ri_session;
	crypto_mechanism_t	ri_mech;
	crypto_key_t		ri_key;
} crypto_verify_recover_init_t;

typedef struct crypto_verify_recover {
	uint_t			vr_return_value;
	crypto_session_id_t	vr_session;
	size_t			vr_signlen;
	caddr_t			vr_signbuf;
	size_t			vr_datalen;
	caddr_t			vr_databuf;
} crypto_verify_recover_t;

typedef struct crypto_digest_encrypt_update {
	uint_t			eu_return_value;
	crypto_session_id_t	eu_session;
	size_t			eu_datalen;
	caddr_t			eu_databuf;
	size_t			eu_encrlen;
	caddr_t			eu_encrbuf;
} crypto_digest_encrypt_update_t;

typedef struct crypto_decrypt_digest_update {
	uint_t			du_return_value;
	crypto_session_id_t	du_session;
	size_t			du_encrlen;
	caddr_t			du_encrbuf;
	size_t			du_datalen;
	caddr_t			du_databuf;
} crypto_decrypt_digest_update_t;

typedef struct crypto_sign_encrypt_update {
	uint_t			eu_return_value;
	crypto_session_id_t	eu_session;
	size_t			eu_datalen;
	caddr_t			eu_databuf;
	size_t			eu_encrlen;
	caddr_t			eu_encrbuf;
} crypto_sign_encrypt_update_t;

typedef struct crypto_decrypt_verify_update {
	uint_t			vu_return_value;
	crypto_session_id_t	vu_session;
	size_t			vu_encrlen;
	caddr_t			vu_encrbuf;
	size_t			vu_datalen;
	caddr_t			vu_databuf;
} crypto_decrypt_verify_update_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_encrypt32 {
	uint32_t		ce_return_value;
	crypto_session_id_t	ce_session;
	size32_t		ce_datalen;
	caddr32_t		ce_databuf;
	size32_t		ce_encrlen;
	caddr32_t		ce_encrbuf;
	uint32_t		ce_flags;
} crypto_encrypt32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_encrypt_init32 {
	uint32_t		ei_return_value;
	crypto_session_id_t	ei_session;
	crypto_mechanism32_t	ei_mech;
	crypto_key32_t		ei_key;
} crypto_encrypt_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_encrypt_update32 {
	uint32_t		eu_return_value;
	crypto_session_id_t	eu_session;
	size32_t		eu_datalen;
	caddr32_t		eu_databuf;
	size32_t		eu_encrlen;
	caddr32_t		eu_encrbuf;
} crypto_encrypt_update32_t;

typedef struct crypto_encrypt_final32 {
	uint32_t		ef_return_value;
	crypto_session_id_t	ef_session;
	size32_t		ef_encrlen;
	caddr32_t		ef_encrbuf;
} crypto_encrypt_final32_t;

typedef struct crypto_decrypt32 {
	uint32_t		cd_return_value;
	crypto_session_id_t	cd_session;
	size32_t		cd_encrlen;
	caddr32_t		cd_encrbuf;
	size32_t		cd_datalen;
	caddr32_t		cd_databuf;
	uint32_t		cd_flags;
} crypto_decrypt32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_decrypt_init32 {
	uint32_t		di_return_value;
	crypto_session_id_t	di_session;
	crypto_mechanism32_t	di_mech;
	crypto_key32_t		di_key;
} crypto_decrypt_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_decrypt_update32 {
	uint32_t		du_return_value;
	crypto_session_id_t	du_session;
	size32_t		du_encrlen;
	caddr32_t		du_encrbuf;
	size32_t		du_datalen;
	caddr32_t		du_databuf;
} crypto_decrypt_update32_t;

typedef struct crypto_decrypt_final32 {
	uint32_t		df_return_value;
	crypto_session_id_t	df_session;
	size32_t		df_datalen;
	caddr32_t		df_databuf;
} crypto_decrypt_final32_t;

typedef struct crypto_digest32 {
	uint32_t		cd_return_value;
	crypto_session_id_t	cd_session;
	size32_t		cd_datalen;
	caddr32_t		cd_databuf;
	size32_t		cd_digestlen;
	caddr32_t		cd_digestbuf;
} crypto_digest32_t;

typedef struct crypto_digest_init32 {
	uint32_t		di_return_value;
	crypto_session_id_t	di_session;
	crypto_mechanism32_t	di_mech;
} crypto_digest_init32_t;

typedef struct crypto_digest_update32 {
	uint32_t		du_return_value;
	crypto_session_id_t	du_session;
	size32_t		du_datalen;
	caddr32_t		du_databuf;
} crypto_digest_update32_t;

typedef struct crypto_digest_key32 {
	uint32_t		dk_return_value;
	crypto_session_id_t	dk_session;
	crypto_key32_t		dk_key;
} crypto_digest_key32_t;

typedef struct crypto_digest_final32 {
	uint32_t		df_return_value;
	crypto_session_id_t	df_session;
	size32_t		df_digestlen;
	caddr32_t		df_digestbuf;
} crypto_digest_final32_t;

typedef struct crypto_mac32 {
	uint32_t		cm_return_value;
	crypto_session_id_t	cm_session;
	size32_t		cm_datalen;
	caddr32_t		cm_databuf;
	size32_t		cm_maclen;
	caddr32_t		cm_macbuf;
} crypto_mac32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_mac_init32 {
	uint32_t		mi_return_value;
	crypto_session_id_t	mi_session;
	crypto_mechanism32_t	mi_mech;
	crypto_key32_t		mi_key;
} crypto_mac_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_mac_update32 {
	uint32_t		mu_return_value;
	crypto_session_id_t	mu_session;
	size32_t		mu_datalen;
	caddr32_t		mu_databuf;
} crypto_mac_update32_t;

typedef struct crypto_mac_final32 {
	uint32_t		mf_return_value;
	crypto_session_id_t	mf_session;
	size32_t		mf_maclen;
	caddr32_t		mf_macbuf;
} crypto_mac_final32_t;

typedef struct crypto_sign32 {
	uint32_t		cs_return_value;
	crypto_session_id_t	cs_session;
	size32_t		cs_datalen;
	caddr32_t		cs_databuf;
	size32_t		cs_signlen;
	caddr32_t		cs_signbuf;
} crypto_sign32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_sign_init32 {
	uint32_t		si_return_value;
	crypto_session_id_t	si_session;
	crypto_mechanism32_t	si_mech;
	crypto_key32_t		si_key;
} crypto_sign_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_sign_update32 {
	uint32_t		su_return_value;
	crypto_session_id_t	su_session;
	size32_t		su_datalen;
	caddr32_t		su_databuf;
} crypto_sign_update32_t;

typedef struct crypto_sign_final32 {
	uint32_t		sf_return_value;
	crypto_session_id_t	sf_session;
	size32_t		sf_signlen;
	caddr32_t		sf_signbuf;
} crypto_sign_final32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_sign_recover_init32 {
	uint32_t		ri_return_value;
	crypto_session_id_t	ri_session;
	crypto_mechanism32_t	ri_mech;
	crypto_key32_t		ri_key;
} crypto_sign_recover_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_sign_recover32 {
	uint32_t		sr_return_value;
	crypto_session_id_t	sr_session;
	size32_t		sr_datalen;
	caddr32_t		sr_databuf;
	size32_t		sr_signlen;
	caddr32_t		sr_signbuf;
} crypto_sign_recover32_t;

typedef struct crypto_verify32 {
	uint32_t		cv_return_value;
	crypto_session_id_t	cv_session;
	size32_t		cv_datalen;
	caddr32_t		cv_databuf;
	size32_t		cv_signlen;
	caddr32_t		cv_signbuf;
} crypto_verify32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_verify_init32 {
	uint32_t		vi_return_value;
	crypto_session_id_t	vi_session;
	crypto_mechanism32_t	vi_mech;
	crypto_key32_t		vi_key;
} crypto_verify_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_verify_update32 {
	uint32_t		vu_return_value;
	crypto_session_id_t	vu_session;
	size32_t		vu_datalen;
	caddr32_t		vu_databuf;
} crypto_verify_update32_t;

typedef struct crypto_verify_final32 {
	uint32_t		vf_return_value;
	crypto_session_id_t	vf_session;
	size32_t		vf_signlen;
	caddr32_t		vf_signbuf;
} crypto_verify_final32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_verify_recover_init32 {
	uint32_t		ri_return_value;
	crypto_session_id_t	ri_session;
	crypto_mechanism32_t	ri_mech;
	crypto_key32_t		ri_key;
} crypto_verify_recover_init32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_verify_recover32 {
	uint32_t		vr_return_value;
	crypto_session_id_t	vr_session;
	size32_t		vr_signlen;
	caddr32_t		vr_signbuf;
	size32_t		vr_datalen;
	caddr32_t		vr_databuf;
} crypto_verify_recover32_t;

typedef struct crypto_digest_encrypt_update32 {
	uint32_t		eu_return_value;
	crypto_session_id_t	eu_session;
	size32_t		eu_datalen;
	caddr32_t		eu_databuf;
	size32_t		eu_encrlen;
	caddr32_t		eu_encrbuf;
} crypto_digest_encrypt_update32_t;

typedef struct crypto_decrypt_digest_update32 {
	uint32_t		du_return_value;
	crypto_session_id_t	du_session;
	size32_t		du_encrlen;
	caddr32_t		du_encrbuf;
	size32_t		du_datalen;
	caddr32_t		du_databuf;
} crypto_decrypt_digest_update32_t;

typedef struct crypto_sign_encrypt_update32 {
	uint32_t		eu_return_value;
	crypto_session_id_t	eu_session;
	size32_t		eu_datalen;
	caddr32_t		eu_databuf;
	size32_t		eu_encrlen;
	caddr32_t		eu_encrbuf;
} crypto_sign_encrypt_update32_t;

typedef struct crypto_decrypt_verify_update32 {
	uint32_t		vu_return_value;
	crypto_session_id_t	vu_session;
	size32_t		vu_encrlen;
	caddr32_t		vu_encrbuf;
	size32_t		vu_datalen;
	caddr32_t		vu_databuf;
} crypto_decrypt_verify_update32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_ENCRYPT			CRYPTO(50)
#define	CRYPTO_ENCRYPT_INIT		CRYPTO(51)
#define	CRYPTO_ENCRYPT_UPDATE		CRYPTO(52)
#define	CRYPTO_ENCRYPT_FINAL		CRYPTO(53)
#define	CRYPTO_DECRYPT			CRYPTO(54)
#define	CRYPTO_DECRYPT_INIT		CRYPTO(55)
#define	CRYPTO_DECRYPT_UPDATE		CRYPTO(56)
#define	CRYPTO_DECRYPT_FINAL		CRYPTO(57)

#define	CRYPTO_DIGEST			CRYPTO(58)
#define	CRYPTO_DIGEST_INIT		CRYPTO(59)
#define	CRYPTO_DIGEST_UPDATE		CRYPTO(60)
#define	CRYPTO_DIGEST_KEY		CRYPTO(61)
#define	CRYPTO_DIGEST_FINAL		CRYPTO(62)
#define	CRYPTO_MAC			CRYPTO(63)
#define	CRYPTO_MAC_INIT			CRYPTO(64)
#define	CRYPTO_MAC_UPDATE		CRYPTO(65)
#define	CRYPTO_MAC_FINAL		CRYPTO(66)

#define	CRYPTO_SIGN			CRYPTO(67)
#define	CRYPTO_SIGN_INIT		CRYPTO(68)
#define	CRYPTO_SIGN_UPDATE		CRYPTO(69)
#define	CRYPTO_SIGN_FINAL		CRYPTO(70)
#define	CRYPTO_SIGN_RECOVER_INIT	CRYPTO(71)
#define	CRYPTO_SIGN_RECOVER		CRYPTO(72)
#define	CRYPTO_VERIFY			CRYPTO(73)
#define	CRYPTO_VERIFY_INIT		CRYPTO(74)
#define	CRYPTO_VERIFY_UPDATE		CRYPTO(75)
#define	CRYPTO_VERIFY_FINAL		CRYPTO(76)
#define	CRYPTO_VERIFY_RECOVER_INIT	CRYPTO(77)
#define	CRYPTO_VERIFY_RECOVER		CRYPTO(78)

#define	CRYPTO_DIGEST_ENCRYPT_UPDATE	CRYPTO(79)
#define	CRYPTO_DECRYPT_DIGEST_UPDATE	CRYPTO(80)
#define	CRYPTO_SIGN_ENCRYPT_UPDATE	CRYPTO(81)
#define	CRYPTO_DECRYPT_VERIFY_UPDATE	CRYPTO(82)

/*
 * Random Number Ioctls
 */
typedef struct crypto_seed_random {
	uint_t			sr_return_value;
	crypto_session_id_t	sr_session;
	size_t			sr_seedlen;
	caddr_t			sr_seedbuf;
} crypto_seed_random_t;

typedef struct crypto_generate_random {
	uint_t			gr_return_value;
	crypto_session_id_t	gr_session;
	caddr_t			gr_buf;
	size_t			gr_buflen;
} crypto_generate_random_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_seed_random32 {
	uint32_t		sr_return_value;
	crypto_session_id_t	sr_session;
	size32_t		sr_seedlen;
	caddr32_t		sr_seedbuf;
} crypto_seed_random32_t;

typedef struct crypto_generate_random32 {
	uint32_t		gr_return_value;
	crypto_session_id_t	gr_session;
	caddr32_t		gr_buf;
	size32_t		gr_buflen;
} crypto_generate_random32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_SEED_RANDOM		CRYPTO(90)
#define	CRYPTO_GENERATE_RANDOM		CRYPTO(91)

/*
 * Object Management Ioctls
 */
typedef struct crypto_object_create {
	uint_t			oc_return_value;
	crypto_session_id_t	oc_session;
	crypto_object_id_t	oc_handle;
	uint_t			oc_count;
	caddr_t			oc_attributes;
} crypto_object_create_t;

typedef struct crypto_object_copy {
	uint_t			oc_return_value;
	crypto_session_id_t	oc_session;
	crypto_object_id_t	oc_handle;
	crypto_object_id_t	oc_new_handle;
	uint_t			oc_count;
	caddr_t			oc_new_attributes;
} crypto_object_copy_t;

typedef struct crypto_object_destroy {
	uint_t			od_return_value;
	crypto_session_id_t	od_session;
	crypto_object_id_t	od_handle;
} crypto_object_destroy_t;

typedef struct crypto_object_get_attribute_value {
	uint_t			og_return_value;
	crypto_session_id_t	og_session;
	crypto_object_id_t	og_handle;
	uint_t			og_count;
	caddr_t			og_attributes;
} crypto_object_get_attribute_value_t;

typedef struct crypto_object_get_size {
	uint_t			gs_return_value;
	crypto_session_id_t	gs_session;
	crypto_object_id_t	gs_handle;
	size_t			gs_size;
} crypto_object_get_size_t;

typedef struct crypto_object_set_attribute_value {
	uint_t			sa_return_value;
	crypto_session_id_t	sa_session;
	crypto_object_id_t	sa_handle;
	uint_t			sa_count;
	caddr_t			sa_attributes;
} crypto_object_set_attribute_value_t;

typedef struct crypto_object_find_init {
	uint_t			fi_return_value;
	crypto_session_id_t	fi_session;
	uint_t			fi_count;
	caddr_t			fi_attributes;
} crypto_object_find_init_t;

typedef struct crypto_object_find_update {
	uint_t			fu_return_value;
	crypto_session_id_t	fu_session;
	uint_t			fu_max_count;
	uint_t			fu_count;
	caddr_t			fu_handles;
} crypto_object_find_update_t;

typedef struct crypto_object_find_final {
	uint_t			ff_return_value;
	crypto_session_id_t	ff_session;
} crypto_object_find_final_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_object_create32 {
	uint32_t		oc_return_value;
	crypto_session_id_t	oc_session;
	crypto_object_id_t	oc_handle;
	uint32_t		oc_count;
	caddr32_t		oc_attributes;
} crypto_object_create32_t;

typedef struct crypto_object_copy32 {
	uint32_t		oc_return_value;
	crypto_session_id_t	oc_session;
	crypto_object_id_t	oc_handle;
	crypto_object_id_t	oc_new_handle;
	uint32_t		oc_count;
	caddr32_t		oc_new_attributes;
} crypto_object_copy32_t;

typedef struct crypto_object_destroy32 {
	uint32_t		od_return_value;
	crypto_session_id_t	od_session;
	crypto_object_id_t	od_handle;
} crypto_object_destroy32_t;

typedef struct crypto_object_get_attribute_value32 {
	uint32_t		og_return_value;
	crypto_session_id_t	og_session;
	crypto_object_id_t	og_handle;
	uint32_t		og_count;
	caddr32_t		og_attributes;
} crypto_object_get_attribute_value32_t;

typedef struct crypto_object_get_size32 {
	uint32_t		gs_return_value;
	crypto_session_id_t	gs_session;
	crypto_object_id_t	gs_handle;
	size32_t		gs_size;
} crypto_object_get_size32_t;

typedef struct crypto_object_set_attribute_value32 {
	uint32_t		sa_return_value;
	crypto_session_id_t	sa_session;
	crypto_object_id_t	sa_handle;
	uint32_t		sa_count;
	caddr32_t		sa_attributes;
} crypto_object_set_attribute_value32_t;

typedef struct crypto_object_find_init32 {
	uint32_t		fi_return_value;
	crypto_session_id_t	fi_session;
	uint32_t		fi_count;
	caddr32_t		fi_attributes;
} crypto_object_find_init32_t;

typedef struct crypto_object_find_update32 {
	uint32_t		fu_return_value;
	crypto_session_id_t	fu_session;
	uint32_t		fu_max_count;
	uint32_t		fu_count;
	caddr32_t		fu_handles;
} crypto_object_find_update32_t;

typedef struct crypto_object_find_final32 {
	uint32_t		ff_return_value;
	crypto_session_id_t	ff_session;
} crypto_object_find_final32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_OBJECT_CREATE			CRYPTO(100)
#define	CRYPTO_OBJECT_COPY			CRYPTO(101)
#define	CRYPTO_OBJECT_DESTROY			CRYPTO(102)
#define	CRYPTO_OBJECT_GET_ATTRIBUTE_VALUE	CRYPTO(103)
#define	CRYPTO_OBJECT_GET_SIZE			CRYPTO(104)
#define	CRYPTO_OBJECT_SET_ATTRIBUTE_VALUE	CRYPTO(105)
#define	CRYPTO_OBJECT_FIND_INIT			CRYPTO(106)
#define	CRYPTO_OBJECT_FIND_UPDATE		CRYPTO(107)
#define	CRYPTO_OBJECT_FIND_FINAL		CRYPTO(108)

/*
 * Key Generation Ioctls
 */
typedef struct crypto_object_generate_key {
	uint_t			gk_return_value;
	crypto_session_id_t	gk_session;
	crypto_object_id_t	gk_handle;
	crypto_mechanism_t	gk_mechanism;
	uint_t			gk_count;
	caddr_t			gk_attributes;
} crypto_object_generate_key_t;

typedef struct crypto_object_generate_key_pair {
	uint_t			kp_return_value;
	crypto_session_id_t	kp_session;
	crypto_object_id_t	kp_public_handle;
	crypto_object_id_t	kp_private_handle;
	uint_t			kp_public_count;
	uint_t			kp_private_count;
	caddr_t			kp_public_attributes;
	caddr_t			kp_private_attributes;
	crypto_mechanism_t	kp_mechanism;
} crypto_object_generate_key_pair_t;

typedef struct crypto_object_wrap_key {
	uint_t			wk_return_value;
	crypto_session_id_t	wk_session;
	crypto_mechanism_t	wk_mechanism;
	crypto_key_t		wk_wrapping_key;
	crypto_object_id_t	wk_object_handle;
	size_t			wk_wrapped_key_len;
	caddr_t			wk_wrapped_key;
} crypto_object_wrap_key_t;

typedef struct crypto_object_unwrap_key {
	uint_t			uk_return_value;
	crypto_session_id_t	uk_session;
	crypto_mechanism_t	uk_mechanism;
	crypto_key_t		uk_unwrapping_key;
	crypto_object_id_t	uk_object_handle;
	size_t			uk_wrapped_key_len;
	caddr_t			uk_wrapped_key;
	uint_t			uk_count;
	caddr_t			uk_attributes;
} crypto_object_unwrap_key_t;

typedef struct crypto_derive_key {
	uint_t			dk_return_value;
	crypto_session_id_t	dk_session;
	crypto_mechanism_t	dk_mechanism;
	crypto_key_t		dk_base_key;
	crypto_object_id_t	dk_object_handle;
	uint_t			dk_count;
	caddr_t			dk_attributes;
} crypto_derive_key_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_object_generate_key32 {
	uint32_t		gk_return_value;
	crypto_session_id_t	gk_session;
	crypto_object_id_t	gk_handle;
	crypto_mechanism32_t	gk_mechanism;
	uint32_t		gk_count;
	caddr32_t		gk_attributes;
} crypto_object_generate_key32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

typedef struct crypto_object_generate_key_pair32 {
	uint32_t		kp_return_value;
	crypto_session_id_t	kp_session;
	crypto_object_id_t	kp_public_handle;
	crypto_object_id_t	kp_private_handle;
	uint32_t		kp_public_count;
	uint32_t		kp_private_count;
	caddr32_t		kp_public_attributes;
	caddr32_t		kp_private_attributes;
	crypto_mechanism32_t	kp_mechanism;
} crypto_object_generate_key_pair32_t;

typedef struct crypto_object_wrap_key32 {
	uint32_t		wk_return_value;
	crypto_session_id_t	wk_session;
	crypto_mechanism32_t	wk_mechanism;
	crypto_key32_t		wk_wrapping_key;
	crypto_object_id_t	wk_object_handle;
	size32_t		wk_wrapped_key_len;
	caddr32_t		wk_wrapped_key;
} crypto_object_wrap_key32_t;

typedef struct crypto_object_unwrap_key32 {
	uint32_t		uk_return_value;
	crypto_session_id_t	uk_session;
	crypto_mechanism32_t	uk_mechanism;
	crypto_key32_t		uk_unwrapping_key;
	crypto_object_id_t	uk_object_handle;
	size32_t		uk_wrapped_key_len;
	caddr32_t		uk_wrapped_key;
	uint32_t		uk_count;
	caddr32_t		uk_attributes;
} crypto_object_unwrap_key32_t;

typedef struct crypto_derive_key32 {
	uint32_t		dk_return_value;
	crypto_session_id_t	dk_session;
	crypto_mechanism32_t	dk_mechanism;
	crypto_key32_t		dk_base_key;
	crypto_object_id_t	dk_object_handle;
	uint32_t		dk_count;
	caddr32_t		dk_attributes;
} crypto_derive_key32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_GENERATE_KEY		CRYPTO(110)
#define	CRYPTO_GENERATE_KEY_PAIR	CRYPTO(111)
#define	CRYPTO_WRAP_KEY			CRYPTO(112)
#define	CRYPTO_UNWRAP_KEY		CRYPTO(113)
#define	CRYPTO_DERIVE_KEY		CRYPTO(114)

/*
 * Provider Management Ioctls
 */

typedef struct crypto_get_provider_list {
	uint_t			pl_return_value;
	uint_t			pl_count;
	crypto_provider_entry_t	pl_list[1];
} crypto_get_provider_list_t;

typedef struct crypto_provider_data {
	uchar_t			pd_prov_desc[CRYPTO_PROVIDER_DESCR_MAX_LEN];
	uchar_t			pd_label[CRYPTO_EXT_SIZE_LABEL];
	uchar_t			pd_manufacturerID[CRYPTO_EXT_SIZE_MANUF];
	uchar_t			pd_model[CRYPTO_EXT_SIZE_MODEL];
	uchar_t			pd_serial_number[CRYPTO_EXT_SIZE_SERIAL];
	ulong_t			pd_flags;
	ulong_t			pd_max_session_count;
	ulong_t			pd_session_count;
	ulong_t			pd_max_rw_session_count;
	ulong_t			pd_rw_session_count;
	ulong_t			pd_max_pin_len;
	ulong_t			pd_min_pin_len;
	ulong_t			pd_total_public_memory;
	ulong_t			pd_free_public_memory;
	ulong_t			pd_total_private_memory;
	ulong_t			pd_free_private_memory;
	crypto_version_t	pd_hardware_version;
	crypto_version_t	pd_firmware_version;
	uchar_t			pd_time[CRYPTO_EXT_SIZE_TIME];
} crypto_provider_data_t;

typedef struct crypto_get_provider_info {
	uint_t			gi_return_value;
	crypto_provider_id_t	gi_provider_id;
	crypto_provider_data_t	gi_provider_data;
} crypto_get_provider_info_t;

typedef struct crypto_get_provider_mechanisms {
	uint_t			pm_return_value;
	crypto_provider_id_t	pm_provider_id;
	uint_t			pm_count;
	crypto_mech_name_t	pm_list[1];
} crypto_get_provider_mechanisms_t;

typedef struct crypto_get_provider_mechanism_info {
	uint_t			mi_return_value;
	crypto_provider_id_t	mi_provider_id;
	crypto_mech_name_t	mi_mechanism_name;
	uint32_t		mi_min_key_size;
	uint32_t		mi_max_key_size;
	uint32_t		mi_flags;
} crypto_get_provider_mechanism_info_t;

typedef struct crypto_init_token {
	uint_t			it_return_value;
	crypto_provider_id_t	it_provider_id;
	caddr_t			it_pin;
	size_t			it_pin_len;
	caddr_t			it_label;
} crypto_init_token_t;

typedef struct crypto_init_pin {
	uint_t			ip_return_value;
	crypto_session_id_t	ip_session;
	caddr_t			ip_pin;
	size_t			ip_pin_len;
} crypto_init_pin_t;

typedef struct crypto_set_pin {
	uint_t			sp_return_value;
	crypto_session_id_t	sp_session;
	caddr_t			sp_old_pin;
	size_t			sp_old_len;
	caddr_t			sp_new_pin;
	size_t			sp_new_len;
} crypto_set_pin_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_get_provider_list32 {
	uint32_t		pl_return_value;
	uint32_t		pl_count;
	crypto_provider_entry_t pl_list[1];
} crypto_get_provider_list32_t;

typedef struct crypto_version32 {
	uchar_t	cv_major;
	uchar_t	cv_minor;
} crypto_version32_t;

typedef struct crypto_provider_data32 {
	uchar_t			pd_prov_desc[CRYPTO_PROVIDER_DESCR_MAX_LEN];
	uchar_t			pd_label[CRYPTO_EXT_SIZE_LABEL];
	uchar_t			pd_manufacturerID[CRYPTO_EXT_SIZE_MANUF];
	uchar_t			pd_model[CRYPTO_EXT_SIZE_MODEL];
	uchar_t			pd_serial_number[CRYPTO_EXT_SIZE_SERIAL];
	uint32_t		pd_flags;
	uint32_t		pd_max_session_count;
	uint32_t		pd_session_count;
	uint32_t		pd_max_rw_session_count;
	uint32_t		pd_rw_session_count;
	uint32_t		pd_max_pin_len;
	uint32_t		pd_min_pin_len;
	uint32_t		pd_total_public_memory;
	uint32_t		pd_free_public_memory;
	uint32_t		pd_total_private_memory;
	uint32_t		pd_free_private_memory;
	crypto_version32_t	pd_hardware_version;
	crypto_version32_t	pd_firmware_version;
	uchar_t			pd_time[CRYPTO_EXT_SIZE_TIME];
} crypto_provider_data32_t;

typedef struct crypto_get_provider_info32 {
	uint32_t		gi_return_value;
	crypto_provider_id_t	gi_provider_id;
	crypto_provider_data32_t gi_provider_data;
} crypto_get_provider_info32_t;

typedef struct crypto_get_provider_mechanisms32 {
	uint32_t		pm_return_value;
	crypto_provider_id_t	pm_provider_id;
	uint32_t		pm_count;
	crypto_mech_name_t	pm_list[1];
} crypto_get_provider_mechanisms32_t;

typedef struct crypto_init_token32 {
	uint32_t		it_return_value;
	crypto_provider_id_t	it_provider_id;
	caddr32_t		it_pin;
	size32_t		it_pin_len;
	caddr32_t		it_label;
} crypto_init_token32_t;

typedef struct crypto_init_pin32 {
	uint32_t		ip_return_value;
	crypto_session_id_t	ip_session;
	caddr32_t		ip_pin;
	size32_t		ip_pin_len;
} crypto_init_pin32_t;

typedef struct crypto_set_pin32 {
	uint32_t		sp_return_value;
	crypto_session_id_t	sp_session;
	caddr32_t		sp_old_pin;
	size32_t		sp_old_len;
	caddr32_t		sp_new_pin;
	size32_t		sp_new_len;
} crypto_set_pin32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_GET_PROVIDER_LIST		CRYPTO(120)
#define	CRYPTO_GET_PROVIDER_INFO		CRYPTO(121)
#define	CRYPTO_GET_PROVIDER_MECHANISMS		CRYPTO(122)
#define	CRYPTO_GET_PROVIDER_MECHANISM_INFO	CRYPTO(123)
#define	CRYPTO_INIT_TOKEN			CRYPTO(124)
#define	CRYPTO_INIT_PIN				CRYPTO(125)
#define	CRYPTO_SET_PIN				CRYPTO(126)

/*
 * No (Key) Store Key Generation Ioctls
 */
typedef struct crypto_nostore_generate_key {
	uint_t			ngk_return_value;
	crypto_session_id_t	ngk_session;
	crypto_mechanism_t	ngk_mechanism;
	uint_t			ngk_in_count;
	uint_t			ngk_out_count;
	caddr_t			ngk_in_attributes;
	caddr_t			ngk_out_attributes;
} crypto_nostore_generate_key_t;

typedef struct crypto_nostore_generate_key_pair {
	uint_t			nkp_return_value;
	crypto_session_id_t	nkp_session;
	uint_t			nkp_in_public_count;
	uint_t			nkp_in_private_count;
	uint_t			nkp_out_public_count;
	uint_t			nkp_out_private_count;
	caddr_t			nkp_in_public_attributes;
	caddr_t			nkp_in_private_attributes;
	caddr_t			nkp_out_public_attributes;
	caddr_t			nkp_out_private_attributes;
	crypto_mechanism_t	nkp_mechanism;
} crypto_nostore_generate_key_pair_t;

typedef struct crypto_nostore_derive_key {
	uint_t			ndk_return_value;
	crypto_session_id_t	ndk_session;
	crypto_mechanism_t	ndk_mechanism;
	crypto_key_t		ndk_base_key;
	uint_t			ndk_in_count;
	uint_t			ndk_out_count;
	caddr_t			ndk_in_attributes;
	caddr_t			ndk_out_attributes;
} crypto_nostore_derive_key_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_nostore_generate_key32 {
	uint32_t		ngk_return_value;
	crypto_session_id_t	ngk_session;
	crypto_mechanism32_t	ngk_mechanism;
	uint32_t		ngk_in_count;
	uint32_t		ngk_out_count;
	caddr32_t		ngk_in_attributes;
	caddr32_t		ngk_out_attributes;
} crypto_nostore_generate_key32_t;

typedef struct crypto_nostore_generate_key_pair32 {
	uint32_t		nkp_return_value;
	crypto_session_id_t	nkp_session;
	uint32_t		nkp_in_public_count;
	uint32_t		nkp_in_private_count;
	uint32_t		nkp_out_public_count;
	uint32_t		nkp_out_private_count;
	caddr32_t		nkp_in_public_attributes;
	caddr32_t		nkp_in_private_attributes;
	caddr32_t		nkp_out_public_attributes;
	caddr32_t		nkp_out_private_attributes;
	crypto_mechanism32_t	nkp_mechanism;
} crypto_nostore_generate_key_pair32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack(4)
#endif

typedef struct crypto_nostore_derive_key32 {
	uint32_t		ndk_return_value;
	crypto_session_id_t	ndk_session;
	crypto_mechanism32_t	ndk_mechanism;
	crypto_key32_t		ndk_base_key;
	uint32_t		ndk_in_count;
	uint32_t		ndk_out_count;
	caddr32_t		ndk_in_attributes;
	caddr32_t		ndk_out_attributes;
} crypto_nostore_derive_key32_t;

#if _LONG_LONG_ALIGNMENT == 8 && _LONG_LONG_ALIGNMENT_32 == 4
#pragma pack()
#endif

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_NOSTORE_GENERATE_KEY		CRYPTO(127)
#define	CRYPTO_NOSTORE_GENERATE_KEY_PAIR	CRYPTO(128)
#define	CRYPTO_NOSTORE_DERIVE_KEY		CRYPTO(129)

/*
 * Mechanism Ioctls
 */

typedef struct crypto_get_mechanism_list {
	uint_t			ml_return_value;
	uint_t			ml_count;
	crypto_mech_name_t	ml_list[1];
} crypto_get_mechanism_list_t;

typedef struct crypto_get_all_mechanism_info {
	uint_t			mi_return_value;
	crypto_mech_name_t	mi_mechanism_name;
	uint_t			mi_count;
	crypto_mechanism_info_t	mi_list[1];
} crypto_get_all_mechanism_info_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_get_mechanism_list32 {
	uint32_t		ml_return_value;
	uint32_t		ml_count;
	crypto_mech_name_t	ml_list[1];
} crypto_get_mechanism_list32_t;

typedef struct crypto_get_all_mechanism_info32 {
	uint32_t		mi_return_value;
	crypto_mech_name_t	mi_mechanism_name;
	uint32_t		mi_count;
	crypto_mechanism_info32_t mi_list[1];
} crypto_get_all_mechanism_info32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_GET_MECHANISM_LIST		CRYPTO(140)
#define	CRYPTO_GET_ALL_MECHANISM_INFO		CRYPTO(141)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_IOCTL_H */
