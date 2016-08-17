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

#ifndef _SYS_CRYPTO_OPS_IMPL_H
#define	_SYS_CRYPTO_OPS_IMPL_H

/*
 * Scheduler internal structures.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/api.h>
#include <sys/crypto/spi.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/common.h>

/*
 * The parameters needed for each function group are batched
 * in one structure. This is much simpler than having a
 * separate structure for each function.
 *
 * In some cases, a field is generically named to keep the
 * structure small. The comments indicate these cases.
 */
typedef struct kcf_digest_ops_params {
	crypto_session_id_t	do_sid;
	crypto_mech_type_t	do_framework_mechtype;
	crypto_mechanism_t	do_mech;
	crypto_data_t		*do_data;
	crypto_data_t		*do_digest;
	crypto_key_t		*do_digest_key;	/* Argument for digest_key() */
} kcf_digest_ops_params_t;

typedef struct kcf_mac_ops_params {
	crypto_session_id_t		mo_sid;
	crypto_mech_type_t		mo_framework_mechtype;
	crypto_mechanism_t		mo_mech;
	crypto_key_t			*mo_key;
	crypto_data_t			*mo_data;
	crypto_data_t			*mo_mac;
	crypto_spi_ctx_template_t	mo_templ;
} kcf_mac_ops_params_t;

typedef struct kcf_encrypt_ops_params {
	crypto_session_id_t		eo_sid;
	crypto_mech_type_t		eo_framework_mechtype;
	crypto_mechanism_t		eo_mech;
	crypto_key_t			*eo_key;
	crypto_data_t			*eo_plaintext;
	crypto_data_t			*eo_ciphertext;
	crypto_spi_ctx_template_t	eo_templ;
} kcf_encrypt_ops_params_t;

typedef struct kcf_decrypt_ops_params {
	crypto_session_id_t		dop_sid;
	crypto_mech_type_t		dop_framework_mechtype;
	crypto_mechanism_t		dop_mech;
	crypto_key_t			*dop_key;
	crypto_data_t			*dop_ciphertext;
	crypto_data_t			*dop_plaintext;
	crypto_spi_ctx_template_t	dop_templ;
} kcf_decrypt_ops_params_t;

typedef struct kcf_sign_ops_params {
	crypto_session_id_t		so_sid;
	crypto_mech_type_t		so_framework_mechtype;
	crypto_mechanism_t		so_mech;
	crypto_key_t			*so_key;
	crypto_data_t			*so_data;
	crypto_data_t			*so_signature;
	crypto_spi_ctx_template_t	so_templ;
} kcf_sign_ops_params_t;

typedef struct kcf_verify_ops_params {
	crypto_session_id_t		vo_sid;
	crypto_mech_type_t		vo_framework_mechtype;
	crypto_mechanism_t		vo_mech;
	crypto_key_t			*vo_key;
	crypto_data_t			*vo_data;
	crypto_data_t			*vo_signature;
	crypto_spi_ctx_template_t	vo_templ;
} kcf_verify_ops_params_t;

typedef struct kcf_encrypt_mac_ops_params {
	crypto_session_id_t 		em_sid;
	crypto_mech_type_t		em_framework_encr_mechtype;
	crypto_mechanism_t		em_encr_mech;
	crypto_key_t			*em_encr_key;
	crypto_mech_type_t		em_framework_mac_mechtype;
	crypto_mechanism_t		em_mac_mech;
	crypto_key_t			*em_mac_key;
	crypto_data_t			*em_plaintext;
	crypto_dual_data_t		*em_ciphertext;
	crypto_data_t			*em_mac;
	crypto_spi_ctx_template_t	em_encr_templ;
	crypto_spi_ctx_template_t	em_mac_templ;
} kcf_encrypt_mac_ops_params_t;

typedef struct kcf_mac_decrypt_ops_params {
	crypto_session_id_t 		md_sid;
	crypto_mech_type_t		md_framework_mac_mechtype;
	crypto_mechanism_t		md_mac_mech;
	crypto_key_t			*md_mac_key;
	crypto_mech_type_t		md_framework_decr_mechtype;
	crypto_mechanism_t		md_decr_mech;
	crypto_key_t			*md_decr_key;
	crypto_dual_data_t		*md_ciphertext;
	crypto_data_t			*md_mac;
	crypto_data_t			*md_plaintext;
	crypto_spi_ctx_template_t	md_mac_templ;
	crypto_spi_ctx_template_t	md_decr_templ;
} kcf_mac_decrypt_ops_params_t;

typedef struct kcf_random_number_ops_params {
	crypto_session_id_t	rn_sid;
	uchar_t			*rn_buf;
	size_t			rn_buflen;
	uint_t			rn_entropy_est;
	uint32_t		rn_flags;
} kcf_random_number_ops_params_t;

/*
 * so_pd is useful when the provider descriptor (pd) supplying the
 * provider handle is different from the pd supplying the ops vector.
 * This is the case for session open/close where so_pd can be the pd
 * of a logical provider. The pd supplying the ops vector is passed
 * as an argument to kcf_submit_request().
 */
typedef struct kcf_session_ops_params {
	crypto_session_id_t	*so_sid_ptr;
	crypto_session_id_t	so_sid;
	crypto_user_type_t	so_user_type;
	char			*so_pin;
	size_t			so_pin_len;
	kcf_provider_desc_t	*so_pd;
} kcf_session_ops_params_t;

typedef struct kcf_object_ops_params {
	crypto_session_id_t		oo_sid;
	crypto_object_id_t		oo_object_id;
	crypto_object_attribute_t	*oo_template;
	uint_t 				oo_attribute_count;
	crypto_object_id_t		*oo_object_id_ptr;
	size_t				*oo_object_size;
	void				**oo_find_init_pp_ptr;
	void				*oo_find_pp;
	uint_t				oo_max_object_count;
	uint_t				*oo_object_count_ptr;
} kcf_object_ops_params_t;

/*
 * ko_key is used to encode wrapping key in key_wrap() and
 * unwrapping key in key_unwrap(). ko_key_template and
 * ko_key_attribute_count are used to encode public template
 * and public template attr count in key_generate_pair().
 * kops->ko_key_object_id_ptr is used to encode public key
 * in key_generate_pair().
 */
typedef struct kcf_key_ops_params {
	crypto_session_id_t		ko_sid;
	crypto_mech_type_t		ko_framework_mechtype;
	crypto_mechanism_t		ko_mech;
	crypto_object_attribute_t	*ko_key_template;
	uint_t				ko_key_attribute_count;
	crypto_object_id_t		*ko_key_object_id_ptr;
	crypto_object_attribute_t	*ko_private_key_template;
	uint_t				ko_private_key_attribute_count;
	crypto_object_id_t		*ko_private_key_object_id_ptr;
	crypto_key_t			*ko_key;
	uchar_t				*ko_wrapped_key;
	size_t				*ko_wrapped_key_len_ptr;
	crypto_object_attribute_t	*ko_out_template1;
	crypto_object_attribute_t	*ko_out_template2;
	uint_t				ko_out_attribute_count1;
	uint_t				ko_out_attribute_count2;
} kcf_key_ops_params_t;

/*
 * po_pin and po_pin_len are used to encode new_pin and new_pin_len
 * when wrapping set_pin() function parameters.
 *
 * po_pd is useful when the provider descriptor (pd) supplying the
 * provider handle is different from the pd supplying the ops vector.
 * This is true for the ext_info provider entry point where po_pd
 * can be the pd of a logical provider. The pd supplying the ops vector
 * is passed as an argument to kcf_submit_request().
 */
typedef struct kcf_provmgmt_ops_params {
	crypto_session_id_t 		po_sid;
	char				*po_pin;
	size_t				po_pin_len;
	char				*po_old_pin;
	size_t				po_old_pin_len;
	char				*po_label;
	crypto_provider_ext_info_t	*po_ext_info;
	kcf_provider_desc_t		*po_pd;
} kcf_provmgmt_ops_params_t;

/*
 * The operation type within a function group.
 */
typedef enum kcf_op_type {
	/* common ops for all mechanisms */
	KCF_OP_INIT = 1,
	KCF_OP_SINGLE,	/* pkcs11 sense. So, INIT is already done */
	KCF_OP_UPDATE,
	KCF_OP_FINAL,
	KCF_OP_ATOMIC,

	/* digest_key op */
	KCF_OP_DIGEST_KEY,

	/* mac specific op */
	KCF_OP_MAC_VERIFY_ATOMIC,

	/* mac/cipher specific op */
	KCF_OP_MAC_VERIFY_DECRYPT_ATOMIC,

	/* sign_recover ops */
	KCF_OP_SIGN_RECOVER_INIT,
	KCF_OP_SIGN_RECOVER,
	KCF_OP_SIGN_RECOVER_ATOMIC,

	/* verify_recover ops */
	KCF_OP_VERIFY_RECOVER_INIT,
	KCF_OP_VERIFY_RECOVER,
	KCF_OP_VERIFY_RECOVER_ATOMIC,

	/* random number ops */
	KCF_OP_RANDOM_SEED,
	KCF_OP_RANDOM_GENERATE,

	/* session management ops */
	KCF_OP_SESSION_OPEN,
	KCF_OP_SESSION_CLOSE,
	KCF_OP_SESSION_LOGIN,
	KCF_OP_SESSION_LOGOUT,

	/* object management ops */
	KCF_OP_OBJECT_CREATE,
	KCF_OP_OBJECT_COPY,
	KCF_OP_OBJECT_DESTROY,
	KCF_OP_OBJECT_GET_SIZE,
	KCF_OP_OBJECT_GET_ATTRIBUTE_VALUE,
	KCF_OP_OBJECT_SET_ATTRIBUTE_VALUE,
	KCF_OP_OBJECT_FIND_INIT,
	KCF_OP_OBJECT_FIND,
	KCF_OP_OBJECT_FIND_FINAL,

	/* key management ops */
	KCF_OP_KEY_GENERATE,
	KCF_OP_KEY_GENERATE_PAIR,
	KCF_OP_KEY_WRAP,
	KCF_OP_KEY_UNWRAP,
	KCF_OP_KEY_DERIVE,
	KCF_OP_KEY_CHECK,

	/* provider management ops */
	KCF_OP_MGMT_EXTINFO,
	KCF_OP_MGMT_INITTOKEN,
	KCF_OP_MGMT_INITPIN,
	KCF_OP_MGMT_SETPIN
} kcf_op_type_t;

/*
 * The operation groups that need wrapping of parameters. This is somewhat
 * similar to the function group type in spi.h except that this also includes
 * all the functions that don't have a mechanism.
 *
 * The wrapper macros should never take these enum values as an argument.
 * Rather, they are assigned in the macro itself since they are known
 * from the macro name.
 */
typedef enum kcf_op_group {
	KCF_OG_DIGEST = 1,
	KCF_OG_MAC,
	KCF_OG_ENCRYPT,
	KCF_OG_DECRYPT,
	KCF_OG_SIGN,
	KCF_OG_VERIFY,
	KCF_OG_ENCRYPT_MAC,
	KCF_OG_MAC_DECRYPT,
	KCF_OG_RANDOM,
	KCF_OG_SESSION,
	KCF_OG_OBJECT,
	KCF_OG_KEY,
	KCF_OG_PROVMGMT,
	KCF_OG_NOSTORE_KEY
} kcf_op_group_t;

/*
 * The kcf_op_type_t enum values used here should be only for those
 * operations for which there is a k-api routine in sys/crypto/api.h.
 */
#define	IS_INIT_OP(ftype)	((ftype) == KCF_OP_INIT)
#define	IS_SINGLE_OP(ftype)	((ftype) == KCF_OP_SINGLE)
#define	IS_UPDATE_OP(ftype)	((ftype) == KCF_OP_UPDATE)
#define	IS_FINAL_OP(ftype)	((ftype) == KCF_OP_FINAL)
#define	IS_ATOMIC_OP(ftype)	( \
	(ftype) == KCF_OP_ATOMIC || (ftype) == KCF_OP_MAC_VERIFY_ATOMIC || \
	(ftype) == KCF_OP_MAC_VERIFY_DECRYPT_ATOMIC || \
	(ftype) == KCF_OP_SIGN_RECOVER_ATOMIC || \
	(ftype) == KCF_OP_VERIFY_RECOVER_ATOMIC)

/*
 * Keep the parameters associated with a request around.
 * We need to pass them to the SPI.
 */
typedef struct kcf_req_params {
	kcf_op_group_t		rp_opgrp;
	kcf_op_type_t		rp_optype;

	union {
		kcf_digest_ops_params_t		digest_params;
		kcf_mac_ops_params_t		mac_params;
		kcf_encrypt_ops_params_t	encrypt_params;
		kcf_decrypt_ops_params_t	decrypt_params;
		kcf_sign_ops_params_t		sign_params;
		kcf_verify_ops_params_t		verify_params;
		kcf_encrypt_mac_ops_params_t	encrypt_mac_params;
		kcf_mac_decrypt_ops_params_t	mac_decrypt_params;
		kcf_random_number_ops_params_t	random_number_params;
		kcf_session_ops_params_t	session_params;
		kcf_object_ops_params_t		object_params;
		kcf_key_ops_params_t		key_params;
		kcf_provmgmt_ops_params_t	provmgmt_params;
	} rp_u;
} kcf_req_params_t;


/*
 * The ioctl/k-api code should bundle the parameters into a kcf_req_params_t
 * structure before calling a scheduler routine. The following macros are
 * available for that purpose.
 *
 * For the most part, the macro arguments closely correspond to the
 * function parameters. In some cases, we use generic names. The comments
 * for the structure should indicate these cases.
 */
#define	KCF_WRAP_DIGEST_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_data, _digest) {						\
	kcf_digest_ops_params_t *dops = &(req)->rp_u.digest_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_DIGEST;				\
	(req)->rp_optype = ftype;					\
	dops->do_sid = _sid;						\
	if (mechp != NULL) {						\
		dops->do_mech = *mechp;					\
		dops->do_framework_mechtype = mechp->cm_type;		\
	}								\
	dops->do_digest_key = _key;					\
	dops->do_data = _data;						\
	dops->do_digest = _digest;					\
}

#define	KCF_WRAP_MAC_OPS_PARAMS(req, ftype, _sid, _mech, _key,		\
	_data, _mac, _templ) {						\
	kcf_mac_ops_params_t *mops = &(req)->rp_u.mac_params;		\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_MAC;					\
	(req)->rp_optype = ftype;					\
	mops->mo_sid = _sid;						\
	if (mechp != NULL) {						\
		mops->mo_mech = *mechp;					\
		mops->mo_framework_mechtype = mechp->cm_type;		\
	}								\
	mops->mo_key = _key;						\
	mops->mo_data = _data;						\
	mops->mo_mac = _mac;						\
	mops->mo_templ = _templ;					\
}

#define	KCF_WRAP_ENCRYPT_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_plaintext, _ciphertext, _templ) {				\
	kcf_encrypt_ops_params_t *cops = &(req)->rp_u.encrypt_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_ENCRYPT;				\
	(req)->rp_optype = ftype;					\
	cops->eo_sid = _sid;						\
	if (mechp != NULL) {						\
		cops->eo_mech = *mechp;					\
		cops->eo_framework_mechtype = mechp->cm_type;		\
	}								\
	cops->eo_key = _key;						\
	cops->eo_plaintext = _plaintext;				\
	cops->eo_ciphertext = _ciphertext;				\
	cops->eo_templ = _templ;					\
}

#define	KCF_WRAP_DECRYPT_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_ciphertext, _plaintext, _templ) {				\
	kcf_decrypt_ops_params_t *cops = &(req)->rp_u.decrypt_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_DECRYPT;				\
	(req)->rp_optype = ftype;					\
	cops->dop_sid = _sid;						\
	if (mechp != NULL) {						\
		cops->dop_mech = *mechp;				\
		cops->dop_framework_mechtype = mechp->cm_type;		\
	}								\
	cops->dop_key = _key;						\
	cops->dop_ciphertext = _ciphertext;				\
	cops->dop_plaintext = _plaintext;				\
	cops->dop_templ = _templ;					\
}

#define	KCF_WRAP_SIGN_OPS_PARAMS(req, ftype, _sid, _mech, _key,		\
	_data, _signature, _templ) {					\
	kcf_sign_ops_params_t *sops = &(req)->rp_u.sign_params;		\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_SIGN;					\
	(req)->rp_optype = ftype;					\
	sops->so_sid = _sid;						\
	if (mechp != NULL) {						\
		sops->so_mech = *mechp;					\
		sops->so_framework_mechtype = mechp->cm_type;		\
	}								\
	sops->so_key = _key;						\
	sops->so_data = _data;						\
	sops->so_signature = _signature;				\
	sops->so_templ = _templ;					\
}

#define	KCF_WRAP_VERIFY_OPS_PARAMS(req, ftype, _sid, _mech, _key,	\
	_data, _signature, _templ) {					\
	kcf_verify_ops_params_t *vops = &(req)->rp_u.verify_params;	\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_VERIFY;				\
	(req)->rp_optype = ftype;					\
	vops->vo_sid = _sid;						\
	if (mechp != NULL) {						\
		vops->vo_mech = *mechp;					\
		vops->vo_framework_mechtype = mechp->cm_type;		\
	}								\
	vops->vo_key = _key;						\
	vops->vo_data = _data;						\
	vops->vo_signature = _signature;				\
	vops->vo_templ = _templ;					\
}

#define	KCF_WRAP_ENCRYPT_MAC_OPS_PARAMS(req, ftype, _sid, _encr_key,	\
	_mac_key, _plaintext, _ciphertext, _mac, _encr_templ, _mac_templ) { \
	kcf_encrypt_mac_ops_params_t *cmops = &(req)->rp_u.encrypt_mac_params; \
									\
	(req)->rp_opgrp = KCF_OG_ENCRYPT_MAC;				\
	(req)->rp_optype = ftype;					\
	cmops->em_sid = _sid;						\
	cmops->em_encr_key = _encr_key;					\
	cmops->em_mac_key = _mac_key;					\
	cmops->em_plaintext = _plaintext;				\
	cmops->em_ciphertext = _ciphertext;				\
	cmops->em_mac = _mac;						\
	cmops->em_encr_templ = _encr_templ;				\
	cmops->em_mac_templ = _mac_templ;				\
}

#define	KCF_WRAP_MAC_DECRYPT_OPS_PARAMS(req, ftype, _sid, _mac_key,	\
	_decr_key, _ciphertext, _mac, _plaintext, _mac_templ, _decr_templ) { \
	kcf_mac_decrypt_ops_params_t *cmops = &(req)->rp_u.mac_decrypt_params; \
									\
	(req)->rp_opgrp = KCF_OG_MAC_DECRYPT;				\
	(req)->rp_optype = ftype;					\
	cmops->md_sid = _sid;						\
	cmops->md_mac_key = _mac_key;					\
	cmops->md_decr_key = _decr_key;					\
	cmops->md_ciphertext = _ciphertext;				\
	cmops->md_mac = _mac;						\
	cmops->md_plaintext = _plaintext;				\
	cmops->md_mac_templ = _mac_templ;				\
	cmops->md_decr_templ = _decr_templ;				\
}

#define	KCF_WRAP_RANDOM_OPS_PARAMS(req, ftype, _sid, _buf, _buflen,	\
	_est, _flags) {							\
	kcf_random_number_ops_params_t *rops =				\
		&(req)->rp_u.random_number_params;			\
									\
	(req)->rp_opgrp = KCF_OG_RANDOM;				\
	(req)->rp_optype = ftype;					\
	rops->rn_sid = _sid;						\
	rops->rn_buf = _buf;						\
	rops->rn_buflen = _buflen;					\
	rops->rn_entropy_est = _est;					\
	rops->rn_flags = _flags;					\
}

#define	KCF_WRAP_SESSION_OPS_PARAMS(req, ftype, _sid_ptr, _sid,		\
	_user_type, _pin, _pin_len, _pd) {				\
	kcf_session_ops_params_t *sops = &(req)->rp_u.session_params;	\
									\
	(req)->rp_opgrp = KCF_OG_SESSION;				\
	(req)->rp_optype = ftype;					\
	sops->so_sid_ptr = _sid_ptr;					\
	sops->so_sid = _sid;						\
	sops->so_user_type = _user_type;				\
	sops->so_pin = _pin;						\
	sops->so_pin_len = _pin_len;					\
	sops->so_pd = _pd;						\
}

#define	KCF_WRAP_OBJECT_OPS_PARAMS(req, ftype, _sid, _object_id,	\
	_template, _attribute_count, _object_id_ptr, _object_size,	\
	_find_init_pp_ptr, _find_pp, _max_object_count, _object_count_ptr) { \
	kcf_object_ops_params_t *jops = &(req)->rp_u.object_params;	\
									\
	(req)->rp_opgrp = KCF_OG_OBJECT;				\
	(req)->rp_optype = ftype;					\
	jops->oo_sid = _sid;						\
	jops->oo_object_id = _object_id;				\
	jops->oo_template = _template;					\
	jops->oo_attribute_count = _attribute_count;			\
	jops->oo_object_id_ptr = _object_id_ptr;			\
	jops->oo_object_size = _object_size;				\
	jops->oo_find_init_pp_ptr = _find_init_pp_ptr;			\
	jops->oo_find_pp = _find_pp;					\
	jops->oo_max_object_count = _max_object_count;			\
	jops->oo_object_count_ptr = _object_count_ptr;			\
}

#define	KCF_WRAP_KEY_OPS_PARAMS(req, ftype, _sid, _mech, _key_template, \
	_key_attribute_count, _key_object_id_ptr, _private_key_template, \
	_private_key_attribute_count, _private_key_object_id_ptr,	\
	_key, _wrapped_key, _wrapped_key_len_ptr) {			\
	kcf_key_ops_params_t *kops = &(req)->rp_u.key_params;		\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_KEY;					\
	(req)->rp_optype = ftype;					\
	kops->ko_sid = _sid;						\
	if (mechp != NULL) {						\
		kops->ko_mech = *mechp;					\
		kops->ko_framework_mechtype = mechp->cm_type;		\
	}								\
	kops->ko_key_template = _key_template;				\
	kops->ko_key_attribute_count = _key_attribute_count;		\
	kops->ko_key_object_id_ptr = _key_object_id_ptr;		\
	kops->ko_private_key_template = _private_key_template;		\
	kops->ko_private_key_attribute_count = _private_key_attribute_count; \
	kops->ko_private_key_object_id_ptr = _private_key_object_id_ptr; \
	kops->ko_key = _key;						\
	kops->ko_wrapped_key = _wrapped_key;				\
	kops->ko_wrapped_key_len_ptr = _wrapped_key_len_ptr;		\
}

#define	KCF_WRAP_PROVMGMT_OPS_PARAMS(req, ftype, _sid, _old_pin,	\
	_old_pin_len, _pin, _pin_len, _label, _ext_info, _pd) {		\
	kcf_provmgmt_ops_params_t *pops = &(req)->rp_u.provmgmt_params;	\
									\
	(req)->rp_opgrp = KCF_OG_PROVMGMT;				\
	(req)->rp_optype = ftype;					\
	pops->po_sid = _sid;						\
	pops->po_pin = _pin;						\
	pops->po_pin_len = _pin_len;					\
	pops->po_old_pin = _old_pin;					\
	pops->po_old_pin_len = _old_pin_len;				\
	pops->po_label = _label;					\
	pops->po_ext_info = _ext_info;					\
	pops->po_pd = _pd;						\
}

#define	KCF_WRAP_NOSTORE_KEY_OPS_PARAMS(req, ftype, _sid, _mech,	\
	_key_template, _key_attribute_count, _private_key_template,	\
	_private_key_attribute_count, _key, _out_template1,		\
	_out_attribute_count1, _out_template2, _out_attribute_count2) {	\
	kcf_key_ops_params_t *kops = &(req)->rp_u.key_params;		\
	crypto_mechanism_t *mechp = _mech;				\
									\
	(req)->rp_opgrp = KCF_OG_NOSTORE_KEY;				\
	(req)->rp_optype = ftype;					\
	kops->ko_sid = _sid;						\
	if (mechp != NULL) {						\
		kops->ko_mech = *mechp;					\
		kops->ko_framework_mechtype = mechp->cm_type;		\
	}								\
	kops->ko_key_template = _key_template;				\
	kops->ko_key_attribute_count = _key_attribute_count;		\
	kops->ko_key_object_id_ptr = NULL;				\
	kops->ko_private_key_template = _private_key_template;		\
	kops->ko_private_key_attribute_count = _private_key_attribute_count; \
	kops->ko_private_key_object_id_ptr = NULL;			\
	kops->ko_key = _key;						\
	kops->ko_wrapped_key = NULL;					\
	kops->ko_wrapped_key_len_ptr = 0;				\
	kops->ko_out_template1 = _out_template1;			\
	kops->ko_out_template2 = _out_template2;			\
	kops->ko_out_attribute_count1 = _out_attribute_count1;		\
	kops->ko_out_attribute_count2 = _out_attribute_count2;		\
}

#define	KCF_SET_PROVIDER_MECHNUM(fmtype, pd, mechp)			\
	(mechp)->cm_type =						\
	    KCF_TO_PROV_MECHNUM(pd, fmtype);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_CRYPTO_OPS_IMPL_H */
