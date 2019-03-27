#-
# Copyright (c) 2008 Isilon Inc http://www.isilon.com/
# Authors: Doug Rabson <dfr@rabson.org>
# Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

# Interface for the in-kernel part of a GSS-API mechanism

#include <kgssapi/gssapi.h>
#include "gssd.h"

INTERFACE kgss;

METHOD void init {
	gss_ctx_id_t ctx;
};

METHOD OM_uint32 import {
	gss_ctx_id_t ctx;
	enum sec_context_format format;
	const gss_buffer_t context_token;
};

METHOD void delete {
	gss_ctx_id_t ctx;
	gss_buffer_t output_token;
};

METHOD gss_OID mech_type {
	gss_ctx_id_t ctx;
};

METHOD OM_uint32 get_mic {
	gss_ctx_id_t ctx;
	OM_uint32 *minor_status;
	gss_qop_t qop_req;
	struct mbuf *message_buffer;
	struct mbuf **message_token;
};

METHOD OM_uint32 verify_mic {
	gss_ctx_id_t ctx;
	OM_uint32 *minor_status;
	struct mbuf *message_buffer;
	struct mbuf *token_buffer;
	gss_qop_t *qop_state;
};

METHOD OM_uint32 wrap {
	gss_ctx_id_t ctx;
	OM_uint32 *minor_status;
	int conf_req_flag;
	gss_qop_t qop_req;
	struct mbuf **message_buffer;
	int *conf_state;
};

METHOD OM_uint32 unwrap {
	gss_ctx_id_t ctx;
	OM_uint32 *minor_status;
	struct mbuf **message_buffer;
	int *conf_state;
	gss_qop_t *qop_state;
};

METHOD OM_uint32 wrap_size_limit {
	gss_ctx_id_t ctx;
	OM_uint32 *minor_status;
	int conf_req_flag;
	gss_qop_t qop_req;
	OM_uint32 req_ouput_size;
	OM_uint32 *max_input_size;
}
