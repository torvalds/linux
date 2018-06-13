/*
 *  linux/net/sunrpc/gss_rpc_upcall.c
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

#include <linux/types.h>
#include <linux/un.h>

#include <linux/sunrpc/svcauth.h>
#include "gss_rpc_upcall.h"

#define GSSPROXY_SOCK_PATHNAME	"/var/run/gssproxy.sock"

#define GSSPROXY_PROGRAM	(400112u)
#define GSSPROXY_VERS_1		(1u)

/*
 * Encoding/Decoding functions
 */

enum {
	GSSX_NULL = 0,	/* Unused */
        GSSX_INDICATE_MECHS = 1,
        GSSX_GET_CALL_CONTEXT = 2,
        GSSX_IMPORT_AND_CANON_NAME = 3,
        GSSX_EXPORT_CRED = 4,
        GSSX_IMPORT_CRED = 5,
        GSSX_ACQUIRE_CRED = 6,
        GSSX_STORE_CRED = 7,
        GSSX_INIT_SEC_CONTEXT = 8,
        GSSX_ACCEPT_SEC_CONTEXT = 9,
        GSSX_RELEASE_HANDLE = 10,
        GSSX_GET_MIC = 11,
        GSSX_VERIFY = 12,
        GSSX_WRAP = 13,
        GSSX_UNWRAP = 14,
        GSSX_WRAP_SIZE_LIMIT = 15,
};

#define PROC(proc, name)				\
[GSSX_##proc] = {					\
	.p_proc   = GSSX_##proc,			\
	.p_encode = gssx_enc_##name,	\
	.p_decode = gssx_dec_##name,	\
	.p_arglen = GSSX_ARG_##name##_sz,		\
	.p_replen = GSSX_RES_##name##_sz, 		\
	.p_statidx = GSSX_##proc,			\
	.p_name   = #proc,				\
}

static const struct rpc_procinfo gssp_procedures[] = {
	PROC(INDICATE_MECHS, indicate_mechs),
        PROC(GET_CALL_CONTEXT, get_call_context),
        PROC(IMPORT_AND_CANON_NAME, import_and_canon_name),
        PROC(EXPORT_CRED, export_cred),
        PROC(IMPORT_CRED, import_cred),
        PROC(ACQUIRE_CRED, acquire_cred),
        PROC(STORE_CRED, store_cred),
        PROC(INIT_SEC_CONTEXT, init_sec_context),
        PROC(ACCEPT_SEC_CONTEXT, accept_sec_context),
        PROC(RELEASE_HANDLE, release_handle),
        PROC(GET_MIC, get_mic),
        PROC(VERIFY, verify),
        PROC(WRAP, wrap),
        PROC(UNWRAP, unwrap),
        PROC(WRAP_SIZE_LIMIT, wrap_size_limit),
};



/*
 * Common transport functions
 */

static const struct rpc_program gssp_program;

static int gssp_rpc_create(struct net *net, struct rpc_clnt **_clnt)
{
	static const struct sockaddr_un gssp_localaddr = {
		.sun_family		= AF_LOCAL,
		.sun_path		= GSSPROXY_SOCK_PATHNAME,
	};
	struct rpc_create_args args = {
		.net		= net,
		.protocol	= XPRT_TRANSPORT_LOCAL,
		.address	= (struct sockaddr *)&gssp_localaddr,
		.addrsize	= sizeof(gssp_localaddr),
		.servername	= "localhost",
		.program	= &gssp_program,
		.version	= GSSPROXY_VERS_1,
		.authflavor	= RPC_AUTH_NULL,
		/*
		 * Note we want connection to be done in the caller's
		 * filesystem namespace.  We therefore turn off the idle
		 * timeout, which would result in reconnections being
		 * done without the correct namespace:
		 */
		.flags		= RPC_CLNT_CREATE_NOPING |
				  RPC_CLNT_CREATE_NO_IDLE_TIMEOUT
	};
	struct rpc_clnt *clnt;
	int result = 0;

	clnt = rpc_create(&args);
	if (IS_ERR(clnt)) {
		dprintk("RPC:       failed to create AF_LOCAL gssproxy "
				"client (errno %ld).\n", PTR_ERR(clnt));
		result = PTR_ERR(clnt);
		*_clnt = NULL;
		goto out;
	}

	dprintk("RPC:       created new gssp local client (gssp_local_clnt: "
			"%p)\n", clnt);
	*_clnt = clnt;

out:
	return result;
}

void init_gssp_clnt(struct sunrpc_net *sn)
{
	mutex_init(&sn->gssp_lock);
	sn->gssp_clnt = NULL;
}

int set_gssp_clnt(struct net *net)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct rpc_clnt *clnt;
	int ret;

	mutex_lock(&sn->gssp_lock);
	ret = gssp_rpc_create(net, &clnt);
	if (!ret) {
		if (sn->gssp_clnt)
			rpc_shutdown_client(sn->gssp_clnt);
		sn->gssp_clnt = clnt;
	}
	mutex_unlock(&sn->gssp_lock);
	return ret;
}

void clear_gssp_clnt(struct sunrpc_net *sn)
{
	mutex_lock(&sn->gssp_lock);
	if (sn->gssp_clnt) {
		rpc_shutdown_client(sn->gssp_clnt);
		sn->gssp_clnt = NULL;
	}
	mutex_unlock(&sn->gssp_lock);
}

static struct rpc_clnt *get_gssp_clnt(struct sunrpc_net *sn)
{
	struct rpc_clnt *clnt;

	mutex_lock(&sn->gssp_lock);
	clnt = sn->gssp_clnt;
	if (clnt)
		atomic_inc(&clnt->cl_count);
	mutex_unlock(&sn->gssp_lock);
	return clnt;
}

static int gssp_call(struct net *net, struct rpc_message *msg)
{
	struct sunrpc_net *sn = net_generic(net, sunrpc_net_id);
	struct rpc_clnt *clnt;
	int status;

	clnt = get_gssp_clnt(sn);
	if (!clnt)
		return -EIO;
	status = rpc_call_sync(clnt, msg, 0);
	if (status < 0) {
		dprintk("gssp: rpc_call returned error %d\n", -status);
		switch (status) {
		case -EPROTONOSUPPORT:
			status = -EINVAL;
			break;
		case -ECONNREFUSED:
		case -ETIMEDOUT:
		case -ENOTCONN:
			status = -EAGAIN;
			break;
		case -ERESTARTSYS:
			if (signalled ())
				status = -EINTR;
			break;
		default:
			break;
		}
	}
	rpc_release_client(clnt);
	return status;
}

static void gssp_free_receive_pages(struct gssx_arg_accept_sec_context *arg)
{
	int i;

	for (i = 0; i < arg->npages && arg->pages[i]; i++)
		__free_page(arg->pages[i]);

	kfree(arg->pages);
}

static int gssp_alloc_receive_pages(struct gssx_arg_accept_sec_context *arg)
{
	arg->npages = DIV_ROUND_UP(NGROUPS_MAX * 4, PAGE_SIZE);
	arg->pages = kcalloc(arg->npages, sizeof(struct page *), GFP_KERNEL);
	/*
	 * XXX: actual pages are allocated by xdr layer in
	 * xdr_partial_copy_from_skb.
	 */
	if (!arg->pages)
		return -ENOMEM;
	return 0;
}

/*
 * Public functions
 */

/* numbers somewhat arbitrary but large enough for current needs */
#define GSSX_MAX_OUT_HANDLE	128
#define GSSX_MAX_SRC_PRINC	256
#define GSSX_KMEMBUF (GSSX_max_output_handle_sz + \
			GSSX_max_oid_sz + \
			GSSX_max_princ_sz + \
			sizeof(struct svc_cred))

int gssp_accept_sec_context_upcall(struct net *net,
				struct gssp_upcall_data *data)
{
	struct gssx_ctx ctxh = {
		.state = data->in_handle
	};
	struct gssx_arg_accept_sec_context arg = {
		.input_token = data->in_token,
	};
	struct gssx_ctx rctxh = {
		/*
		 * pass in the max length we expect for each of these
		 * buffers but let the xdr code kmalloc them:
		 */
		.exported_context_token.len = GSSX_max_output_handle_sz,
		.mech.len = GSS_OID_MAX_LEN,
		.src_name.display_name.len = GSSX_max_princ_sz
	};
	struct gssx_res_accept_sec_context res = {
		.context_handle = &rctxh,
		.output_token = &data->out_token
	};
	struct rpc_message msg = {
		.rpc_proc = &gssp_procedures[GSSX_ACCEPT_SEC_CONTEXT],
		.rpc_argp = &arg,
		.rpc_resp = &res,
		.rpc_cred = NULL, /* FIXME ? */
	};
	struct xdr_netobj client_name = { 0 , NULL };
	int ret;

	if (data->in_handle.len != 0)
		arg.context_handle = &ctxh;
	res.output_token->len = GSSX_max_output_token_sz;

	ret = gssp_alloc_receive_pages(&arg);
	if (ret)
		return ret;

	/* use nfs/ for targ_name ? */

	ret = gssp_call(net, &msg);

	gssp_free_receive_pages(&arg);

	/* we need to fetch all data even in case of error so
	 * that we can free special strctures is they have been allocated */
	data->major_status = res.status.major_status;
	data->minor_status = res.status.minor_status;
	if (res.context_handle) {
		data->out_handle = rctxh.exported_context_token;
		data->mech_oid.len = rctxh.mech.len;
		if (rctxh.mech.data) {
			memcpy(data->mech_oid.data, rctxh.mech.data,
						data->mech_oid.len);
			kfree(rctxh.mech.data);
		}
		client_name = rctxh.src_name.display_name;
	}

	if (res.options.count == 1) {
		gssx_buffer *value = &res.options.data[0].value;
		/* Currently we only decode CREDS_VALUE, if we add
		 * anything else we'll have to loop and match on the
		 * option name */
		if (value->len == 1) {
			/* steal group info from struct svc_cred */
			data->creds = *(struct svc_cred *)value->data;
			data->found_creds = 1;
		}
		/* whether we use it or not, free data */
		kfree(value->data);
	}

	if (res.options.count != 0) {
		kfree(res.options.data);
	}

	/* convert to GSS_NT_HOSTBASED_SERVICE form and set into creds */
	if (data->found_creds && client_name.data != NULL) {
		char *c;

		data->creds.cr_raw_principal = kstrndup(client_name.data,
						client_name.len, GFP_KERNEL);

		data->creds.cr_principal = kstrndup(client_name.data,
						client_name.len, GFP_KERNEL);
		if (data->creds.cr_principal) {
			/* terminate and remove realm part */
			c = strchr(data->creds.cr_principal, '@');
			if (c) {
				*c = '\0';

				/* change service-hostname delimiter */
				c = strchr(data->creds.cr_principal, '/');
				if (c) *c = '@';
			}
			if (!c) {
				/* not a service principal */
				kfree(data->creds.cr_principal);
				data->creds.cr_principal = NULL;
			}
		}
	}
	kfree(client_name.data);

	return ret;
}

void gssp_free_upcall_data(struct gssp_upcall_data *data)
{
	kfree(data->in_handle.data);
	kfree(data->out_handle.data);
	kfree(data->out_token.data);
	free_svc_cred(&data->creds);
}

/*
 * Initialization stuff
 */
static unsigned int gssp_version1_counts[ARRAY_SIZE(gssp_procedures)];
static const struct rpc_version gssp_version1 = {
	.number		= GSSPROXY_VERS_1,
	.nrprocs	= ARRAY_SIZE(gssp_procedures),
	.procs		= gssp_procedures,
	.counts		= gssp_version1_counts,
};

static const struct rpc_version *gssp_version[] = {
	NULL,
	&gssp_version1,
};

static struct rpc_stat gssp_stats;

static const struct rpc_program gssp_program = {
	.name		= "gssproxy",
	.number		= GSSPROXY_PROGRAM,
	.nrvers		= ARRAY_SIZE(gssp_version),
	.version	= gssp_version,
	.stats		= &gssp_stats,
};
