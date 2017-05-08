/*
 * GSS Proxy upcall module
 *
 *  Copyright (C) 2012 Simo Sorce <simo@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_GSS_RPC_XDR_H
#define _LINUX_GSS_RPC_XDR_H

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtsock.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_AUTH
#endif

#define LUCID_OPTION "exported_context_type"
#define LUCID_VALUE  "linux_lucid_v1"
#define CREDS_OPTION "exported_creds_type"
#define CREDS_VALUE  "linux_creds_v1"

typedef struct xdr_netobj gssx_buffer;
typedef struct xdr_netobj utf8string;
typedef struct xdr_netobj gssx_OID;

enum gssx_cred_usage {
	GSSX_C_INITIATE = 1,
	GSSX_C_ACCEPT = 2,
	GSSX_C_BOTH = 3,
};

struct gssx_option {
	gssx_buffer option;
	gssx_buffer value;
};

struct gssx_option_array {
	u32 count;
	struct gssx_option *data;
};

struct gssx_status {
	u64 major_status;
	gssx_OID mech;
	u64 minor_status;
	utf8string major_status_string;
	utf8string minor_status_string;
	gssx_buffer server_ctx;
	struct gssx_option_array options;
};

struct gssx_call_ctx {
	utf8string locale;
	gssx_buffer server_ctx;
	struct gssx_option_array options;
};

struct gssx_name_attr {
	gssx_buffer attr;
	gssx_buffer value;
	struct gssx_option_array extensions;
};

struct gssx_name_attr_array {
	u32 count;
	struct gssx_name_attr *data;
};

struct gssx_name {
	gssx_buffer display_name;
};
typedef struct gssx_name gssx_name;

struct gssx_cred_element {
	gssx_name MN;
	gssx_OID mech;
	u32 cred_usage;
	u64 initiator_time_rec;
	u64 acceptor_time_rec;
	struct gssx_option_array options;
};

struct gssx_cred_element_array {
	u32 count;
	struct gssx_cred_element *data;
};

struct gssx_cred {
	gssx_name desired_name;
	struct gssx_cred_element_array elements;
	gssx_buffer cred_handle_reference;
	u32 needs_release;
};

struct gssx_ctx {
	gssx_buffer exported_context_token;
	gssx_buffer state;
	u32 need_release;
	gssx_OID mech;
	gssx_name src_name;
	gssx_name targ_name;
	u64 lifetime;
	u64 ctx_flags;
	u32 locally_initiated;
	u32 open;
	struct gssx_option_array options;
};

struct gssx_cb {
	u64 initiator_addrtype;
	gssx_buffer initiator_address;
	u64 acceptor_addrtype;
	gssx_buffer acceptor_address;
	gssx_buffer application_data;
};


/* This structure is not defined in the protocol.
 * It is used in the kernel to carry around a big buffer
 * as a set of pages */
struct gssp_in_token {
	struct page **pages;	/* Array of contiguous pages */
	unsigned int page_base;	/* Start of page data */
	unsigned int page_len;	/* Length of page data */
};

struct gssx_arg_accept_sec_context {
	struct gssx_call_ctx call_ctx;
	struct gssx_ctx *context_handle;
	struct gssx_cred *cred_handle;
	struct gssp_in_token input_token;
	struct gssx_cb *input_cb;
	u32 ret_deleg_cred;
	struct gssx_option_array options;
	struct page **pages;
	unsigned int npages;
};

struct gssx_res_accept_sec_context {
	struct gssx_status status;
	struct gssx_ctx *context_handle;
	gssx_buffer *output_token;
	/* struct gssx_cred *delegated_cred_handle; not used in kernel */
	struct gssx_option_array options;
};



#define gssx_enc_indicate_mechs NULL
#define gssx_dec_indicate_mechs NULL
#define gssx_enc_get_call_context NULL
#define gssx_dec_get_call_context NULL
#define gssx_enc_import_and_canon_name NULL
#define gssx_dec_import_and_canon_name NULL
#define gssx_enc_export_cred NULL
#define gssx_dec_export_cred NULL
#define gssx_enc_import_cred NULL
#define gssx_dec_import_cred NULL
#define gssx_enc_acquire_cred NULL
#define gssx_dec_acquire_cred NULL
#define gssx_enc_store_cred NULL
#define gssx_dec_store_cred NULL
#define gssx_enc_init_sec_context NULL
#define gssx_dec_init_sec_context NULL
void gssx_enc_accept_sec_context(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 const void *data);
int gssx_dec_accept_sec_context(struct rpc_rqst *rqstp,
				struct xdr_stream *xdr,
				struct gssx_res_accept_sec_context *res);
#define gssx_enc_release_handle NULL
#define gssx_dec_release_handle NULL
#define gssx_enc_get_mic NULL
#define gssx_dec_get_mic NULL
#define gssx_enc_verify NULL
#define gssx_dec_verify NULL
#define gssx_enc_wrap NULL
#define gssx_dec_wrap NULL
#define gssx_enc_unwrap NULL
#define gssx_dec_unwrap NULL
#define gssx_enc_wrap_size_limit NULL
#define gssx_dec_wrap_size_limit NULL

/* non implemented calls are set to 0 size */
#define GSSX_ARG_indicate_mechs_sz 0
#define GSSX_RES_indicate_mechs_sz 0
#define GSSX_ARG_get_call_context_sz 0
#define GSSX_RES_get_call_context_sz 0
#define GSSX_ARG_import_and_canon_name_sz 0
#define GSSX_RES_import_and_canon_name_sz 0
#define GSSX_ARG_export_cred_sz 0
#define GSSX_RES_export_cred_sz 0
#define GSSX_ARG_import_cred_sz 0
#define GSSX_RES_import_cred_sz 0
#define GSSX_ARG_acquire_cred_sz 0
#define GSSX_RES_acquire_cred_sz 0
#define GSSX_ARG_store_cred_sz 0
#define GSSX_RES_store_cred_sz 0
#define GSSX_ARG_init_sec_context_sz 0
#define GSSX_RES_init_sec_context_sz 0

#define GSSX_default_in_call_ctx_sz (4 + 4 + 4 + \
			8 + sizeof(LUCID_OPTION) + sizeof(LUCID_VALUE) + \
			8 + sizeof(CREDS_OPTION) + sizeof(CREDS_VALUE))
#define GSSX_default_in_ctx_hndl_sz (4 + 4+8 + 4 + 4 + 6*4 + 6*4 + 8 + 8 + \
					4 + 4 + 4)
#define GSSX_default_in_cred_sz 4 /* we send in no cred_handle */
#define GSSX_default_in_token_sz 4 /* does *not* include token data */
#define GSSX_default_in_cb_sz 4 /* we do not use channel bindings */
#define GSSX_ARG_accept_sec_context_sz (GSSX_default_in_call_ctx_sz + \
					GSSX_default_in_ctx_hndl_sz + \
					GSSX_default_in_cred_sz + \
					GSSX_default_in_token_sz + \
					GSSX_default_in_cb_sz + \
					4 /* no deleg creds boolean */ + \
					4) /* empty options */

/* somewhat arbitrary numbers but large enough (we ignore some of the data
 * sent down, but it is part of the protocol so we need enough space to take
 * it in) */
#define GSSX_default_status_sz 8 + 24 + 8 + 256 + 256 + 16 + 4
#define GSSX_max_output_handle_sz 128
#define GSSX_max_oid_sz 16
#define GSSX_max_princ_sz 256
#define GSSX_default_ctx_sz (GSSX_max_output_handle_sz + \
			     16 + 4 + GSSX_max_oid_sz + \
			     2 * GSSX_max_princ_sz + \
			     8 + 8 + 4 + 4 + 4)
#define GSSX_max_output_token_sz 1024
/* grouplist not included; we allocate separate pages for that: */
#define GSSX_max_creds_sz (4 + 4 + 4 /* + NGROUPS_MAX*4 */)
#define GSSX_RES_accept_sec_context_sz (GSSX_default_status_sz + \
					GSSX_default_ctx_sz + \
					GSSX_max_output_token_sz + \
					4 + GSSX_max_creds_sz)

#define GSSX_ARG_release_handle_sz 0
#define GSSX_RES_release_handle_sz 0
#define GSSX_ARG_get_mic_sz 0
#define GSSX_RES_get_mic_sz 0
#define GSSX_ARG_verify_sz 0
#define GSSX_RES_verify_sz 0
#define GSSX_ARG_wrap_sz 0
#define GSSX_RES_wrap_sz 0
#define GSSX_ARG_unwrap_sz 0
#define GSSX_RES_unwrap_sz 0
#define GSSX_ARG_wrap_size_limit_sz 0
#define GSSX_RES_wrap_size_limit_sz 0



#endif /* _LINUX_GSS_RPC_XDR_H */
