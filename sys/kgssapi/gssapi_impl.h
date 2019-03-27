/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
 *
 * $FreeBSD$
 */

#include "gssd.h"

MALLOC_DECLARE(M_GSSAPI);

struct _gss_ctx_id_t {
	KOBJ_FIELDS;
	gssd_ctx_id_t	handle;
};

struct _gss_cred_id_t {
	gssd_cred_id_t	handle;
};

struct _gss_name_t {
	gssd_name_t	handle;
};

struct kgss_mech {
	LIST_ENTRY(kgss_mech) km_link;
	gss_OID		km_mech_type;
	const char	*km_mech_name;
	struct kobj_class *km_class;
};
LIST_HEAD(kgss_mech_list, kgss_mech);

extern CLIENT *kgss_gssd_handle;
extern struct mtx kgss_gssd_lock;
extern struct kgss_mech_list kgss_mechs;

CLIENT *kgss_gssd_client(void);
int kgss_oid_equal(const gss_OID oid1, const gss_OID oid2);
extern void kgss_install_mech(gss_OID mech_type, const char *name,
    struct kobj_class *cls);
extern void kgss_uninstall_mech(gss_OID mech_type);
extern gss_OID kgss_find_mech_by_name(const char *name);
extern const char *kgss_find_mech_by_oid(const gss_OID oid);
extern gss_ctx_id_t kgss_create_context(gss_OID mech_type);
extern void kgss_delete_context(gss_ctx_id_t ctx, gss_buffer_t output_token);
extern OM_uint32 kgss_transfer_context(gss_ctx_id_t ctx);
extern void kgss_copy_buffer(const gss_buffer_t from, gss_buffer_t to);
