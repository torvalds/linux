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

#ifndef	_SYS_CRYPTO_API_H
#define	_SYS_CRYPTO_API_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

typedef long crypto_req_id_t;
typedef void *crypto_bc_t;
typedef void *crypto_context_t;
typedef void *crypto_ctx_template_t;

typedef uint32_t crypto_call_flag_t;

/* crypto_call_flag's values */
#define	CRYPTO_ALWAYS_QUEUE	0x00000001	/* ALWAYS queue the req. */
#define	CRYPTO_NOTIFY_OPDONE	0x00000002	/* Notify intermediate steps */
#define	CRYPTO_SKIP_REQID	0x00000004	/* Skip request ID generation */
#define	CRYPTO_RESTRICTED	0x00000008	/* cannot use restricted prov */

typedef struct {
	crypto_call_flag_t	cr_flag;
	void			(*cr_callback_func)(void *, int);
	void			*cr_callback_arg;
	crypto_req_id_t		cr_reqid;
} crypto_call_req_t;

/*
 * Returns the mechanism type corresponding to a mechanism name.
 */

#define	CRYPTO_MECH_INVALID	((uint64_t)-1)
extern crypto_mech_type_t crypto_mech2id(crypto_mech_name_t name);

/*
 * Create and destroy context templates.
 */
extern int crypto_create_ctx_template(crypto_mechanism_t *mech,
    crypto_key_t *key, crypto_ctx_template_t *tmpl, int kmflag);
extern void crypto_destroy_ctx_template(crypto_ctx_template_t tmpl);

/*
 * Single and multi-part digest operations.
 */
extern int crypto_digest(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_data_t *digest, crypto_call_req_t *cr);
extern int crypto_digest_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_data_t *,
    crypto_call_req_t *);
extern int crypto_digest_init(crypto_mechanism_t *mech, crypto_context_t *ctxp,
    crypto_call_req_t *cr);
extern int crypto_digest_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_context_t *, crypto_call_req_t *);
extern int crypto_digest_update(crypto_context_t ctx, crypto_data_t *data,
    crypto_call_req_t *cr);
extern int crypto_digest_final(crypto_context_t ctx, crypto_data_t *digest,
    crypto_call_req_t *cr);

/*
 * Single and multi-part MAC operations.
 */
extern int crypto_mac(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *mac,
    crypto_call_req_t *cr);
extern int crypto_mac_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_key_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);
extern int crypto_mac_verify(crypto_mechanism_t *mech, crypto_data_t *data,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *mac,
    crypto_call_req_t *cr);
extern int crypto_mac_verify_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_key_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);
extern int crypto_mac_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp, crypto_call_req_t *cr);
extern int crypto_mac_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_ctx_template_t,
    crypto_context_t *, crypto_call_req_t *);
extern int crypto_mac_update(crypto_context_t ctx, crypto_data_t *data,
    crypto_call_req_t *cr);
extern int crypto_mac_final(crypto_context_t ctx, crypto_data_t *data,
    crypto_call_req_t *cr);

/*
 * Single and multi-part sign with private key operations.
 */
extern int crypto_sign(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_data_t *data, crypto_ctx_template_t tmpl,
    crypto_data_t *signature, crypto_call_req_t *cr);
extern int crypto_sign_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);
extern int crypto_sign_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp, crypto_call_req_t *cr);
extern int crypto_sign_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_ctx_template_t,
    crypto_context_t *, crypto_call_req_t *);
extern int crypto_sign_update(crypto_context_t ctx, crypto_data_t *data,
    crypto_call_req_t *cr);
extern int crypto_sign_final(crypto_context_t ctx, crypto_data_t *signature,
    crypto_call_req_t *cr);
extern int crypto_sign_recover_init_prov(crypto_provider_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
    crypto_ctx_template_t tmpl, crypto_context_t *, crypto_call_req_t *);
extern int crypto_sign_recover(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_data_t *data, crypto_ctx_template_t tmpl, crypto_data_t *signature,
    crypto_call_req_t *cr);
extern int crypto_sign_recover_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);

/*
 * Single and multi-part verify with public key operations.
 */
extern int crypto_verify(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_data_t *data, crypto_ctx_template_t tmpl, crypto_data_t *signature,
    crypto_call_req_t *cr);
extern int crypto_verify_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);
extern int crypto_verify_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp, crypto_call_req_t *cr);
extern int crypto_verify_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_ctx_template_t,
    crypto_context_t *, crypto_call_req_t *);
extern int crypto_verify_update(crypto_context_t ctx, crypto_data_t *data,
    crypto_call_req_t *cr);
extern int crypto_verify_final(crypto_context_t ctx, crypto_data_t *signature,
    crypto_call_req_t *cr);
extern int crypto_verify_recover_init_prov(crypto_provider_t,
    crypto_session_id_t, crypto_mechanism_t *, crypto_key_t *,
    crypto_ctx_template_t tmpl, crypto_context_t *, crypto_call_req_t *);
extern int crypto_verify_recover(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_data_t *signature, crypto_ctx_template_t tmpl, crypto_data_t *data,
    crypto_call_req_t *cr);
extern int crypto_verify_recover_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_data_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);

/*
 * Single and multi-part encryption operations.
 */
extern int crypto_encrypt(crypto_mechanism_t *mech, crypto_data_t *plaintext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *ciphertext,
    crypto_call_req_t *cr);
extern int crypto_encrypt_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_key_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);
extern int crypto_encrypt_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp, crypto_call_req_t *cr);
extern int crypto_encrypt_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_ctx_template_t,
    crypto_context_t *, crypto_call_req_t *);
extern int crypto_encrypt_update(crypto_context_t ctx,
    crypto_data_t *plaintext, crypto_data_t *ciphertext,
    crypto_call_req_t *cr);
extern int crypto_encrypt_final(crypto_context_t ctx,
    crypto_data_t *ciphertext, crypto_call_req_t *cr);

/*
 * Single and multi-part decryption operations.
 */
extern int crypto_decrypt(crypto_mechanism_t *mech, crypto_data_t *ciphertext,
    crypto_key_t *key, crypto_ctx_template_t tmpl, crypto_data_t *plaintext,
    crypto_call_req_t *cr);
extern int crypto_decrypt_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_data_t *, crypto_key_t *,
    crypto_ctx_template_t, crypto_data_t *, crypto_call_req_t *);
extern int crypto_decrypt_init(crypto_mechanism_t *mech, crypto_key_t *key,
    crypto_ctx_template_t tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *cr);
extern int crypto_decrypt_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_ctx_template_t,
    crypto_context_t *, crypto_call_req_t *);
extern int crypto_decrypt_update(crypto_context_t ctx,
    crypto_data_t *ciphertext, crypto_data_t *plaintext,
    crypto_call_req_t *cr);
extern int crypto_decrypt_final(crypto_context_t ctx, crypto_data_t *plaintext,
    crypto_call_req_t *cr);

/*
 * Single and multi-part encrypt/MAC dual operations.
 */
extern int crypto_encrypt_mac(crypto_mechanism_t *encr_mech,
    crypto_mechanism_t *mac_mech, crypto_data_t *pt,
    crypto_key_t *encr_key, crypto_key_t *mac_key,
    crypto_ctx_template_t encr_tmpl, crypto_ctx_template_t mac_tmpl,
    crypto_dual_data_t *ct, crypto_data_t *mac, crypto_call_req_t *cr);
extern int crypto_encrypt_mac_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_mechanism_t *, crypto_data_t *,
    crypto_key_t *, crypto_key_t *, crypto_ctx_template_t,
    crypto_ctx_template_t, crypto_dual_data_t *, crypto_data_t *,
    crypto_call_req_t *);
extern int crypto_encrypt_mac_init(crypto_mechanism_t *encr_mech,
    crypto_mechanism_t *mac_mech, crypto_key_t *encr_key,
    crypto_key_t *mac_key, crypto_ctx_template_t encr_tmpl,
    crypto_ctx_template_t mac_tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *cr);
extern int crypto_encrypt_mac_init_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_mechanism_t *, crypto_key_t *, crypto_key_t *,
    crypto_ctx_template_t, crypto_ctx_template_t, crypto_context_t *,
    crypto_call_req_t *);
extern int crypto_encrypt_mac_update(crypto_context_t ctx,
    crypto_data_t *pt, crypto_dual_data_t *ct, crypto_call_req_t *cr);
extern int crypto_encrypt_mac_final(crypto_context_t ctx,
    crypto_dual_data_t *ct, crypto_data_t *mac, crypto_call_req_t *cr);

/*
 * Single and multi-part MAC/decrypt dual operations.
 */
extern int crypto_mac_decrypt(crypto_mechanism_t *mac_mech,
    crypto_mechanism_t *decr_mech, crypto_dual_data_t *ct,
    crypto_key_t *mac_key, crypto_key_t *decr_key,
    crypto_ctx_template_t mac_tmpl, crypto_ctx_template_t decr_tmpl,
    crypto_data_t *mac, crypto_data_t *pt, crypto_call_req_t *cr);
extern int crypto_mac_decrypt_prov(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *mac_mech, crypto_mechanism_t *decr_mech,
    crypto_dual_data_t *ct, crypto_key_t *mac_key, crypto_key_t *decr_key,
    crypto_ctx_template_t mac_tmpl, crypto_ctx_template_t decr_tmpl,
    crypto_data_t *mac, crypto_data_t *pt, crypto_call_req_t *cr);
extern int crypto_mac_verify_decrypt(crypto_mechanism_t *mac_mech,
    crypto_mechanism_t *decr_mech, crypto_dual_data_t *ct,
    crypto_key_t *mac_key, crypto_key_t *decr_key,
    crypto_ctx_template_t mac_tmpl, crypto_ctx_template_t decr_tmpl,
    crypto_data_t *mac, crypto_data_t *pt, crypto_call_req_t *cr);
extern int crypto_mac_verify_decrypt_prov(crypto_provider_t,
    crypto_session_id_t, crypto_mechanism_t *mac_mech,
    crypto_mechanism_t *decr_mech, crypto_dual_data_t *ct,
    crypto_key_t *mac_key, crypto_key_t *decr_key,
    crypto_ctx_template_t mac_tmpl, crypto_ctx_template_t decr_tmpl,
    crypto_data_t *mac, crypto_data_t *pt, crypto_call_req_t *cr);
extern int crypto_mac_decrypt_init(crypto_mechanism_t *mac_mech,
    crypto_mechanism_t *decr_mech, crypto_key_t *mac_key,
    crypto_key_t *decr_key, crypto_ctx_template_t mac_tmpl,
    crypto_ctx_template_t decr_tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *cr);
extern int crypto_mac_decrypt_init_prov(crypto_provider_t,
    crypto_session_id_t, crypto_mechanism_t *mac_mech,
    crypto_mechanism_t *decr_mech, crypto_key_t *mac_key,
    crypto_key_t *decr_key, crypto_ctx_template_t mac_tmpl,
    crypto_ctx_template_t decr_tmpl, crypto_context_t *ctxp,
    crypto_call_req_t *cr);
extern int crypto_mac_decrypt_update(crypto_context_t ctx,
    crypto_dual_data_t *ct, crypto_data_t *pt, crypto_call_req_t *cr);
extern int crypto_mac_decrypt_final(crypto_context_t ctx, crypto_data_t *mac,
    crypto_data_t *pt, crypto_call_req_t *cr);

/* Session Management */
extern int crypto_session_open(crypto_provider_t, crypto_session_id_t *,
    crypto_call_req_t *);
extern int crypto_session_close(crypto_provider_t, crypto_session_id_t,
    crypto_call_req_t *);
extern int crypto_session_login(crypto_provider_t, crypto_session_id_t,
    crypto_user_type_t, char *, size_t, crypto_call_req_t *);
extern int crypto_session_logout(crypto_provider_t, crypto_session_id_t,
    crypto_call_req_t *);

/* Object Management */
extern int crypto_object_copy(crypto_provider_t, crypto_session_id_t,
    crypto_object_id_t, crypto_object_attribute_t *, uint_t,
    crypto_object_id_t *, crypto_call_req_t *);
extern int crypto_object_create(crypto_provider_t, crypto_session_id_t,
    crypto_object_attribute_t *, uint_t, crypto_object_id_t *,
    crypto_call_req_t *);
extern int crypto_object_destroy(crypto_provider_t, crypto_session_id_t,
    crypto_object_id_t, crypto_call_req_t *);
extern int crypto_object_get_attribute_value(crypto_provider_t,
    crypto_session_id_t, crypto_object_id_t, crypto_object_attribute_t *,
    uint_t, crypto_call_req_t *);
extern int crypto_object_get_size(crypto_provider_t, crypto_session_id_t,
    crypto_object_id_t, size_t *, crypto_call_req_t *);
extern int crypto_object_find_final(crypto_provider_t, void *,
    crypto_call_req_t *);
extern int crypto_object_find_init(crypto_provider_t, crypto_session_id_t,
    crypto_object_attribute_t *, uint_t, void **, crypto_call_req_t *);
extern int crypto_object_find(crypto_provider_t, void *, crypto_object_id_t *,
    uint_t *, uint_t, crypto_call_req_t *);
extern int crypto_object_set_attribute_value(crypto_provider_t,
    crypto_session_id_t, crypto_object_id_t, crypto_object_attribute_t *,
    uint_t, crypto_call_req_t *);

/* Key Management */
extern int crypto_key_derive(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_object_attribute_t *,
    uint_t, crypto_object_id_t *, crypto_call_req_t *);
extern int crypto_key_generate(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_object_attribute_t *, uint_t,
    crypto_object_id_t *, crypto_call_req_t *);
extern int crypto_key_generate_pair(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_object_attribute_t *, uint_t,
    crypto_object_attribute_t *, uint_t, crypto_object_id_t *,
    crypto_object_id_t *, crypto_call_req_t *);
extern int crypto_key_unwrap(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, uchar_t *, size_t *,
    crypto_object_attribute_t *, uint_t, crypto_object_id_t *,
    crypto_call_req_t *);
extern int crypto_key_wrap(crypto_provider_t, crypto_session_id_t,
    crypto_mechanism_t *, crypto_key_t *, crypto_object_id_t *, uchar_t *,
    size_t *, crypto_call_req_t *);
extern int crypto_key_check_prov(crypto_provider_t, crypto_mechanism_t *mech,
    crypto_key_t *key);
extern int crypto_key_check(crypto_mechanism_t *mech, crypto_key_t *key);


/*
 * Routines to cancel a single asynchronous request or all asynchronous
 * requests associated with a particular context.
 */
extern void crypto_cancel_req(crypto_req_id_t req);
extern void crypto_cancel_ctx(crypto_context_t ctx);

/*
 * crypto_get_mech_list(9F) allocates and returns the list of currently
 * supported cryptographic mechanisms.
 */
extern crypto_mech_name_t *crypto_get_mech_list(uint_t *count, int kmflag);
extern void crypto_free_mech_list(crypto_mech_name_t *mech_names,
    uint_t count);

extern crypto_provider_t crypto_get_provider(char *, char *, char *);
extern int crypto_get_provinfo(crypto_provider_t, crypto_provider_ext_info_t *);
extern void crypto_release_provider(crypto_provider_t);

/*
 * A kernel consumer can request to be notified when some particular event
 * occurs. The valid events, callback function type, and functions to
 * be called to register or unregister for notification are defined below.
 */

#define	CRYPTO_EVENT_MECHS_CHANGED		0x00000001
#define	CRYPTO_EVENT_PROVIDER_REGISTERED	0x00000002
#define	CRYPTO_EVENT_PROVIDER_UNREGISTERED	0x00000004

typedef enum {
	CRYPTO_MECH_ADDED = 1,
	CRYPTO_MECH_REMOVED
} crypto_event_change_t;

/* The event_arg argument structure for CRYPTO_EVENT_PROVIDERS_CHANGE event */
typedef struct crypto_notify_event_change {
	crypto_mech_name_t ec_mech_name;
	crypto_provider_type_t ec_provider_type;
	crypto_event_change_t ec_change;
} crypto_notify_event_change_t;

typedef void *crypto_notify_handle_t;
typedef void (*crypto_notify_callback_t)(uint32_t event_mask, void *event_arg);

extern crypto_notify_handle_t crypto_notify_events(
    crypto_notify_callback_t nf, uint32_t event_mask);
extern void crypto_unnotify_events(crypto_notify_handle_t);

/*
 * crypto_bufcall(9F) group of routines.
 */
extern crypto_bc_t crypto_bufcall_alloc(void);
extern int crypto_bufcall_free(crypto_bc_t bc);
extern int crypto_bufcall(crypto_bc_t bc, void (*func)(void *arg), void *arg);
extern int crypto_unbufcall(crypto_bc_t bc);

/*
 * To obtain the list of key size ranges supported by a mechanism.
 */

#define	CRYPTO_MECH_USAGE_ENCRYPT	0x00000001
#define	CRYPTO_MECH_USAGE_DECRYPT	0x00000002
#define	CRYPTO_MECH_USAGE_MAC		0x00000004

typedef	uint32_t crypto_mech_usage_t;

typedef struct crypto_mechanism_info {
	size_t mi_min_key_size;
	size_t mi_max_key_size;
	crypto_keysize_unit_t mi_keysize_unit; /* for mi_xxx_key_size */
	crypto_mech_usage_t mi_usage;
} crypto_mechanism_info_t;

#ifdef	_SYSCALL32

typedef struct crypto_mechanism_info32 {
	size32_t mi_min_key_size;
	size32_t mi_max_key_size;
	crypto_keysize_unit_t mi_keysize_unit; /* for mi_xxx_key_size */
	crypto_mech_usage_t mi_usage;
} crypto_mechanism_info32_t;

#endif	/* _SYSCALL32 */

extern int crypto_get_all_mech_info(crypto_mech_type_t,
    crypto_mechanism_info_t **, uint_t *, int);
extern void crypto_free_all_mech_info(crypto_mechanism_info_t *, uint_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_API_H */
