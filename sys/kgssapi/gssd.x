/*-
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
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
 */

/* $FreeBSD$ */

#ifdef RPC_HDR

%#ifdef _KERNEL
%#include <kgssapi/gssapi.h>
%#else
%#include <gssapi/gssapi.h>
%#endif

%extern bool_t xdr_gss_buffer_desc(XDR *xdrs, gss_buffer_desc *buf);
%extern bool_t xdr_gss_OID_desc(XDR *xdrs, gss_OID_desc *oid);
%extern bool_t xdr_gss_OID(XDR *xdrs, gss_OID *oidp);
%extern bool_t xdr_gss_OID_set_desc(XDR *xdrs, gss_OID_set_desc *set);
%extern bool_t xdr_gss_OID_set(XDR *xdrs, gss_OID_set *setp);
%extern bool_t xdr_gss_channel_bindings_t(XDR *xdrs, gss_channel_bindings_t *chp);

#endif

typedef uint64_t gssd_ctx_id_t;
typedef uint64_t gssd_cred_id_t;
typedef uint64_t gssd_name_t;

struct init_sec_context_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gssd_ctx_id_t	ctx;
	gss_OID		actual_mech_type;
	gss_buffer_desc output_token;
	uint32_t	ret_flags;
	uint32_t	time_rec;
};

struct init_sec_context_args {
	uint32_t	uid;
	gssd_cred_id_t	cred;
	gssd_ctx_id_t	ctx;
	gssd_name_t	name;
	gss_OID		mech_type;
	uint32_t	req_flags;
	uint32_t	time_req;
	gss_channel_bindings_t input_chan_bindings;
	gss_buffer_desc input_token;
};

struct accept_sec_context_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gssd_ctx_id_t	ctx;
	gssd_name_t	src_name;
	gss_OID		mech_type;
	gss_buffer_desc	output_token;
	uint32_t	ret_flags;
	uint32_t	time_rec;
	gssd_cred_id_t	delegated_cred_handle;
};

struct accept_sec_context_args {
	gssd_ctx_id_t	ctx;
	gssd_cred_id_t	cred;
	gss_buffer_desc	input_token;
	gss_channel_bindings_t input_chan_bindings;
};

struct delete_sec_context_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gss_buffer_desc	output_token;
};

struct delete_sec_context_args {
	gssd_ctx_id_t	ctx;
};

enum sec_context_format {
	KGSS_HEIMDAL_0_6,
	KGSS_HEIMDAL_1_1
};

struct export_sec_context_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	enum sec_context_format format;
	gss_buffer_desc	interprocess_token;
};

struct export_sec_context_args {
       gssd_ctx_id_t	ctx;
};

struct import_name_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gssd_name_t	output_name;
};

struct import_name_args {
	gss_buffer_desc	input_name_buffer;
	gss_OID		input_name_type;
};

struct canonicalize_name_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gssd_name_t	output_name;
};

struct canonicalize_name_args {
	gssd_name_t	input_name;
	gss_OID		mech_type;
};

struct export_name_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gss_buffer_desc	exported_name;
};

struct export_name_args {
	gssd_name_t	input_name;
};

struct release_name_res {
	uint32_t	major_status;
	uint32_t	minor_status;
};

struct release_name_args {
	gssd_name_t	input_name;
};

struct pname_to_uid_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	uint32_t	uid;
	uint32_t	gid;
	uint32_t	gidlist<>;
};

struct pname_to_uid_args {
       gssd_name_t	pname;
       gss_OID		mech;
};

struct acquire_cred_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	gssd_cred_id_t	output_cred;
	gss_OID_set	actual_mechs;
	uint32_t	time_rec;
};

struct acquire_cred_args {
	uint32_t	uid;
	gssd_name_t	desired_name;
	uint32_t	time_req;
	gss_OID_set	desired_mechs;
	int		cred_usage;
};

struct set_cred_option_res {
	uint32_t	major_status;
	uint32_t	minor_status;
};

struct set_cred_option_args {
       gssd_cred_id_t	cred;
       gss_OID		option_name;
       gss_buffer_desc	option_value;
};

struct release_cred_res {
	uint32_t	major_status;
	uint32_t	minor_status;
};

struct release_cred_args {
	gssd_cred_id_t	cred;
};

struct display_status_res {
	uint32_t	major_status;
	uint32_t	minor_status;
	uint32_t	message_context;
	gss_buffer_desc	status_string;
};

struct display_status_args {
       uint32_t		status_value;
       int		status_type;
       gss_OID		mech_type;
       uint32_t		message_context;
};

program GSSD {
	version GSSDVERS {
		void GSSD_NULL(void) = 0;

		init_sec_context_res
		GSSD_INIT_SEC_CONTEXT(init_sec_context_args) = 1;
			
		accept_sec_context_res
		GSSD_ACCEPT_SEC_CONTEXT(accept_sec_context_args) = 2;
			
		delete_sec_context_res
		GSSD_DELETE_SEC_CONTEXT(delete_sec_context_args) = 3;
			
		export_sec_context_res
		GSSD_EXPORT_SEC_CONTEXT(export_sec_context_args) = 4;
			
		import_name_res
		GSSD_IMPORT_NAME(import_name_args) = 5;

		canonicalize_name_res
		GSSD_CANONICALIZE_NAME(canonicalize_name_args) = 6;

		export_name_res
		GSSD_EXPORT_NAME(export_name_args) = 7;

		release_name_res
		GSSD_RELEASE_NAME(release_name_args) = 8;

		pname_to_uid_res
		GSSD_PNAME_TO_UID(pname_to_uid_args) = 9;

		acquire_cred_res
		GSSD_ACQUIRE_CRED(acquire_cred_args) = 10;

		set_cred_option_res
		GSSD_SET_CRED_OPTION(set_cred_option_args) = 11;

		release_cred_res
		GSSD_RELEASE_CRED(release_cred_args) = 12;

		display_status_res
		GSSD_DISPLAY_STATUS(display_status_args) = 13;
	} = 1;
} = 0x40677373;
