/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

#include <unistd.h>
#include <sys/queue.h>

typedef OM_uint32 _gss_acquire_cred_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* desired_name */
	       OM_uint32,              /* time_req */
	       const gss_OID_set,      /* desired_mechs */
	       gss_cred_usage_t,       /* cred_usage */
	       gss_cred_id_t *,        /* output_cred_handle */
	       gss_OID_set *,          /* actual_mechs */
	       OM_uint32 *             /* time_rec */
	      );

typedef OM_uint32 _gss_release_cred_t
	      (OM_uint32 *,            /* minor_status */
	       gss_cred_id_t *         /* cred_handle */
	      );

typedef OM_uint32 _gss_init_sec_context_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* initiator_cred_handle */
	       gss_ctx_id_t *,         /* context_handle */
	       const gss_name_t,       /* target_name */
	       const gss_OID,          /* mech_type */
	       OM_uint32,              /* req_flags */
	       OM_uint32,              /* time_req */
	       const gss_channel_bindings_t,
				       /* input_chan_bindings */
	       const gss_buffer_t,     /* input_token */
	       gss_OID *,              /* actual_mech_type */
	       gss_buffer_t,           /* output_token */
	       OM_uint32 *,            /* ret_flags */
	       OM_uint32 *             /* time_rec */
	      );

typedef OM_uint32 _gss_accept_sec_context_t
	      (OM_uint32 *,            /* minor_status */
	       gss_ctx_id_t *,         /* context_handle */
	       const gss_cred_id_t,    /* acceptor_cred_handle */
	       const gss_buffer_t,     /* input_token_buffer */
	       const gss_channel_bindings_t,
				       /* input_chan_bindings */
	       gss_name_t *,           /* src_name */
	       gss_OID *,              /* mech_type */
	       gss_buffer_t,           /* output_token */
	       OM_uint32 *,            /* ret_flags */
	       OM_uint32 *,            /* time_rec */
	       gss_cred_id_t *         /* delegated_cred_handle */
	      );

typedef OM_uint32 _gss_process_context_token_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       const gss_buffer_t      /* token_buffer */
	      );

typedef OM_uint32 _gss_delete_sec_context_t
	      (OM_uint32 *,            /* minor_status */
	       gss_ctx_id_t *,         /* context_handle */
	       gss_buffer_t            /* output_token */
	      );

typedef OM_uint32 _gss_context_time_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       OM_uint32 *             /* time_rec */
	      );

typedef OM_uint32 _gss_get_mic_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       gss_qop_t,              /* qop_req */
	       const gss_buffer_t,     /* message_buffer */
	       gss_buffer_t            /* message_token */
	      );

typedef OM_uint32 _gss_verify_mic_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       const gss_buffer_t,     /* message_buffer */
	       const gss_buffer_t,     /* token_buffer */
	       gss_qop_t *             /* qop_state */
	      );

typedef OM_uint32 _gss_wrap_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       int,                    /* conf_req_flag */
	       gss_qop_t,              /* qop_req */
	       const gss_buffer_t,     /* input_message_buffer */
	       int *,                  /* conf_state */
	       gss_buffer_t            /* output_message_buffer */
	      );

typedef OM_uint32 _gss_unwrap_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       const gss_buffer_t,     /* input_message_buffer */
	       gss_buffer_t,           /* output_message_buffer */
	       int *,                  /* conf_state */
	       gss_qop_t *             /* qop_state */
	      );

typedef OM_uint32 _gss_display_status_t
	      (OM_uint32 *,            /* minor_status */
	       OM_uint32,              /* status_value */
	       int,                    /* status_type */
	       const gss_OID,          /* mech_type */
	       OM_uint32 *,            /* message_context */
	       gss_buffer_t            /* status_string */
	      );

typedef OM_uint32 _gss_indicate_mechs_t
	      (OM_uint32 *,            /* minor_status */
	       gss_OID_set *           /* mech_set */
	      );

typedef OM_uint32 _gss_compare_name_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* name1 */
	       const gss_name_t,       /* name2 */
	       int *                   /* name_equal */
	      );

typedef OM_uint32 _gss_display_name_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       gss_buffer_t,           /* output_name_buffer */
	       gss_OID *               /* output_name_type */
	      );

typedef OM_uint32 _gss_import_name_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_buffer_t,     /* input_name_buffer */
	       const gss_OID,          /* input_name_type */
	       gss_name_t *            /* output_name */
	      );

typedef OM_uint32 _gss_export_name_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       gss_buffer_t            /* exported_name */
	      );

typedef OM_uint32 _gss_release_name_t
	      (OM_uint32 *,            /* minor_status */
	       gss_name_t *            /* input_name */
	      );

typedef OM_uint32 _gss_inquire_cred_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* cred_handle */
	       gss_name_t *,           /* name */
	       OM_uint32 *,            /* lifetime */
	       gss_cred_usage_t *,     /* cred_usage */
	       gss_OID_set *           /* mechanisms */
	      );

typedef OM_uint32 _gss_inquire_context_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       gss_name_t *,           /* src_name */
	       gss_name_t *,           /* targ_name */
	       OM_uint32 *,            /* lifetime_rec */
	       gss_OID *,              /* mech_type */
	       OM_uint32 *,            /* ctx_flags */
	       int *,                  /* locally_initiated */
	       int *                   /* open */
	      );

typedef OM_uint32 _gss_wrap_size_limit_t
	      (OM_uint32 *,            /* minor_status */
	       const gss_ctx_id_t,     /* context_handle */
	       int,                    /* conf_req_flag */
	       gss_qop_t,              /* qop_req */
	       OM_uint32,              /* req_output_size */
	       OM_uint32 *             /* max_input_size */
	      );

typedef OM_uint32 _gss_add_cred_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* input_cred_handle */
	       const gss_name_t,       /* desired_name */
	       const gss_OID,          /* desired_mech */
	       gss_cred_usage_t,       /* cred_usage */
	       OM_uint32,              /* initiator_time_req */
	       OM_uint32,              /* acceptor_time_req */
	       gss_cred_id_t *,        /* output_cred_handle */
	       gss_OID_set *,          /* actual_mechs */
	       OM_uint32 *,            /* initiator_time_rec */
	       OM_uint32 *             /* acceptor_time_rec */
	      );

typedef OM_uint32 _gss_inquire_cred_by_mech_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_cred_id_t,    /* cred_handle */
	       const gss_OID,          /* mech_type */
	       gss_name_t *,           /* name */
	       OM_uint32 *,            /* initiator_lifetime */
	       OM_uint32 *,            /* acceptor_lifetime */
	       gss_cred_usage_t *      /* cred_usage */
	      );

typedef OM_uint32 _gss_export_sec_context_t (
	       OM_uint32 *,            /* minor_status */
	       gss_ctx_id_t *,         /* context_handle */
	       gss_buffer_t            /* interprocess_token */
	      );

typedef OM_uint32 _gss_import_sec_context_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_buffer_t,     /* interprocess_token */
	       gss_ctx_id_t *          /* context_handle */
	      );

typedef OM_uint32 _gss_inquire_names_for_mech_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_OID,          /* mechanism */
	       gss_OID_set *           /* name_types */
	      );

typedef OM_uint32 _gss_inquire_mechs_for_name_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       gss_OID_set *           /* mech_types */
	      );

typedef OM_uint32 _gss_canonicalize_name_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* input_name */
	       const gss_OID,          /* mech_type */
	       gss_name_t *            /* output_name */
	      );

typedef OM_uint32 _gss_duplicate_name_t (
	       OM_uint32 *,            /* minor_status */
	       const gss_name_t,       /* src_name */
	       gss_name_t *            /* dest_name */
	      );

typedef OM_uint32 _gss_inquire_sec_context_by_oid
	      (OM_uint32 *,		/* minor_status */
	       const gss_ctx_id_t,	/* context_handle */
	       const gss_OID,		/* desired_object */
	       gss_buffer_set_t *	/* result */
	      );

typedef OM_uint32 _gss_inquire_cred_by_oid
	      (OM_uint32 *,	       /* bminor_status */
	       const gss_cred_id_t,    /* cred_handle, */
	       const gss_OID,	       /* desired_object */
	       gss_buffer_set_t *      /* data_set */
	      );

typedef OM_uint32 _gss_set_sec_context_option
	      (OM_uint32 *,		/* minor status */
	       gss_ctx_id_t *,		/* context */
	       const gss_OID,		/* option to set */
	       const gss_buffer_t	/* option value */
	      );

typedef OM_uint32 _gss_set_cred_option
	      (OM_uint32 *,		/* minor status */
	       gss_cred_id_t *,		/* cred */
	       const gss_OID,		/* option to set */
	       const gss_buffer_t	/* option value */
	      );

typedef OM_uint32 _gss_pseudo_random
	      (OM_uint32 *,	       /* minor status */
	       gss_ctx_id_t,	       /* context */
	       int,		       /* PRF key */
	       const gss_buffer_t,     /* PRF input */
	       ssize_t,		       /* desired output length */
	       gss_buffer_t	       /* PRF output */
	      );

typedef OM_uint32 _gss_pname_to_uid
	      (OM_uint32 *,		/* minor status */
	       gss_name_t pname,	/* principal name */
	       gss_OID mech,		/* mechanism to query */
	       uid_t *uidp		/* pointer to UID for result */
	      );

struct _gss_mech_switch {
	SLIST_ENTRY(_gss_mech_switch)	gm_link;
	const char			*gm_name_prefix;
	gss_OID_desc			gm_mech_oid;
	void				*gm_so;
	_gss_acquire_cred_t		*gm_acquire_cred;
	_gss_release_cred_t		*gm_release_cred;
	_gss_init_sec_context_t		*gm_init_sec_context;
	_gss_accept_sec_context_t	*gm_accept_sec_context;
	_gss_process_context_token_t	*gm_process_context_token;
	_gss_delete_sec_context_t	*gm_delete_sec_context;
	_gss_context_time_t		*gm_context_time;
	_gss_get_mic_t			*gm_get_mic;
	_gss_verify_mic_t		*gm_verify_mic;
	_gss_wrap_t			*gm_wrap;
	_gss_unwrap_t			*gm_unwrap;
	_gss_display_status_t		*gm_display_status;
	_gss_indicate_mechs_t		*gm_indicate_mechs;
	_gss_compare_name_t		*gm_compare_name;
	_gss_display_name_t		*gm_display_name;
	_gss_import_name_t		*gm_import_name;
	_gss_export_name_t		*gm_export_name;
	_gss_release_name_t		*gm_release_name;
	_gss_inquire_cred_t		*gm_inquire_cred;
	_gss_inquire_context_t		*gm_inquire_context;
	_gss_wrap_size_limit_t		*gm_wrap_size_limit;
	_gss_add_cred_t			*gm_add_cred;
	_gss_inquire_cred_by_mech_t	*gm_inquire_cred_by_mech;
	_gss_export_sec_context_t	*gm_export_sec_context;
	_gss_import_sec_context_t	*gm_import_sec_context;
	_gss_inquire_names_for_mech_t	*gm_inquire_names_for_mech;
	_gss_inquire_mechs_for_name_t	*gm_inquire_mechs_for_name;
	_gss_canonicalize_name_t	*gm_canonicalize_name;
	_gss_duplicate_name_t		*gm_duplicate_name;
	_gss_inquire_sec_context_by_oid	*gm_inquire_sec_context_by_oid;
	_gss_inquire_cred_by_oid	*gm_inquire_cred_by_oid;
	_gss_set_sec_context_option	*gm_set_sec_context_option;
	_gss_set_cred_option		*gm_set_cred_option;
	_gss_pseudo_random		*gm_pseudo_random;
	_gss_pname_to_uid		*gm_pname_to_uid;
};
SLIST_HEAD(_gss_mech_switch_list, _gss_mech_switch);
extern struct _gss_mech_switch_list _gss_mechs;
extern gss_OID_set _gss_mech_oids;

extern void _gss_load_mech(void);
extern struct _gss_mech_switch *_gss_find_mech_switch(gss_OID);
extern void _gss_mg_error(struct _gss_mech_switch *m, OM_uint32 maj,
    OM_uint32 min);
extern void _gss_mg_collect_error(gss_OID mech, OM_uint32 maj, OM_uint32 min);
