/*
 *  linux/net/sunrpc/gss_rpc_upcall.h
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

#ifndef _GSS_RPC_UPCALL_H
#define _GSS_RPC_UPCALL_H

#include <linux/sunrpc/gss_api.h>
#include <linux/sunrpc/auth_gss.h>
#include "gss_rpc_xdr.h"
#include "../netns.h"

struct gssp_upcall_data {
	struct xdr_netobj in_handle;
	struct gssp_in_token in_token;
	struct xdr_netobj out_handle;
	struct xdr_netobj out_token;
	struct rpcsec_gss_oid mech_oid;
	struct svc_cred creds;
	int found_creds;
	int major_status;
	int minor_status;
};

int gssp_accept_sec_context_upcall(struct net *net,
				struct gssp_upcall_data *data);
void gssp_free_upcall_data(struct gssp_upcall_data *data);

void init_gssp_clnt(struct sunrpc_net *);
int set_gssp_clnt(struct net *);
void clear_gssp_clnt(struct sunrpc_net *);
#endif /* _GSS_RPC_UPCALL_H */
