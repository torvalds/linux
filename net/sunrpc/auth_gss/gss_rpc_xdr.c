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

#include <linux/sunrpc/svcauth.h>
#include "gss_rpc_xdr.h"

static int gssx_enc_bool(struct xdr_stream *xdr, int v)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;
	*p = v ? xdr_one : xdr_zero;
	return 0;
}

static int gssx_dec_bool(struct xdr_stream *xdr, u32 *v)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;
	*v = be32_to_cpu(*p);
	return 0;
}

static int gssx_enc_buffer(struct xdr_stream *xdr,
			   gssx_buffer *buf)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, sizeof(u32) + buf->len);
	if (!p)
		return -ENOSPC;
	xdr_encode_opaque(p, buf->data, buf->len);
	return 0;
}

static int gssx_enc_in_token(struct xdr_stream *xdr,
			     struct gssp_in_token *in)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return -ENOSPC;
	*p = cpu_to_be32(in->page_len);

	/* all we need to do is to write pages */
	xdr_write_pages(xdr, in->pages, in->page_base, in->page_len);

	return 0;
}


static int gssx_dec_buffer(struct xdr_stream *xdr,
			   gssx_buffer *buf)
{
	u32 length;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;

	length = be32_to_cpup(p);
	p = xdr_inline_decode(xdr, length);
	if (unlikely(p == NULL))
		return -ENOSPC;

	if (buf->len == 0) {
		/* we intentionally are not interested in this buffer */
		return 0;
	}
	if (length > buf->len)
		return -ENOSPC;

	if (!buf->data) {
		buf->data = kmemdup(p, length, GFP_KERNEL);
		if (!buf->data)
			return -ENOMEM;
	} else {
		memcpy(buf->data, p, length);
	}
	buf->len = length;
	return 0;
}

static int gssx_enc_option(struct xdr_stream *xdr,
			   struct gssx_option *opt)
{
	int err;

	err = gssx_enc_buffer(xdr, &opt->option);
	if (err)
		return err;
	err = gssx_enc_buffer(xdr, &opt->value);
	return err;
}

static int gssx_dec_option(struct xdr_stream *xdr,
			   struct gssx_option *opt)
{
	int err;

	err = gssx_dec_buffer(xdr, &opt->option);
	if (err)
		return err;
	err = gssx_dec_buffer(xdr, &opt->value);
	return err;
}

static int dummy_enc_opt_array(struct xdr_stream *xdr,
				struct gssx_option_array *oa)
{
	__be32 *p;

	if (oa->count != 0)
		return -EINVAL;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return -ENOSPC;
	*p = 0;

	return 0;
}

static int dummy_dec_opt_array(struct xdr_stream *xdr,
				struct gssx_option_array *oa)
{
	struct gssx_option dummy;
	u32 count, i;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;
	count = be32_to_cpup(p++);
	memset(&dummy, 0, sizeof(dummy));
	for (i = 0; i < count; i++) {
		gssx_dec_option(xdr, &dummy);
	}

	oa->count = 0;
	oa->data = NULL;
	return 0;
}

static int get_host_u32(struct xdr_stream *xdr, u32 *res)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (!p)
		return -EINVAL;
	/* Contents of linux creds are all host-endian: */
	memcpy(res, p, sizeof(u32));
	return 0;
}

static int gssx_dec_linux_creds(struct xdr_stream *xdr,
				struct svc_cred *creds)
{
	u32 length;
	__be32 *p;
	u32 tmp;
	u32 N;
	int i, err;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;

	length = be32_to_cpup(p);

	if (length > (3 + NGROUPS_MAX) * sizeof(u32))
		return -ENOSPC;

	/* uid */
	err = get_host_u32(xdr, &tmp);
	if (err)
		return err;
	creds->cr_uid = make_kuid(&init_user_ns, tmp);

	/* gid */
	err = get_host_u32(xdr, &tmp);
	if (err)
		return err;
	creds->cr_gid = make_kgid(&init_user_ns, tmp);

	/* number of additional gid's */
	err = get_host_u32(xdr, &tmp);
	if (err)
		return err;
	N = tmp;
	if ((3 + N) * sizeof(u32) != length)
		return -EINVAL;
	creds->cr_group_info = groups_alloc(N);
	if (creds->cr_group_info == NULL)
		return -ENOMEM;

	/* gid's */
	for (i = 0; i < N; i++) {
		kgid_t kgid;
		err = get_host_u32(xdr, &tmp);
		if (err)
			goto out_free_groups;
		err = -EINVAL;
		kgid = make_kgid(&init_user_ns, tmp);
		if (!gid_valid(kgid))
			goto out_free_groups;
		creds->cr_group_info->gid[i] = kgid;
	}

	return 0;
out_free_groups:
	groups_free(creds->cr_group_info);
	return err;
}

static int gssx_dec_option_array(struct xdr_stream *xdr,
				 struct gssx_option_array *oa)
{
	struct svc_cred *creds;
	u32 count, i;
	__be32 *p;
	int err;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;
	count = be32_to_cpup(p++);
	if (!count)
		return 0;

	/* we recognize only 1 currently: CREDS_VALUE */
	oa->count = 1;

	oa->data = kmalloc(sizeof(struct gssx_option), GFP_KERNEL);
	if (!oa->data)
		return -ENOMEM;

	creds = kzalloc(sizeof(struct svc_cred), GFP_KERNEL);
	if (!creds) {
		kfree(oa->data);
		return -ENOMEM;
	}

	oa->data[0].option.data = CREDS_VALUE;
	oa->data[0].option.len = sizeof(CREDS_VALUE);
	oa->data[0].value.data = (void *)creds;
	oa->data[0].value.len = 0;

	for (i = 0; i < count; i++) {
		gssx_buffer dummy = { 0, NULL };
		u32 length;

		/* option buffer */
		p = xdr_inline_decode(xdr, 4);
		if (unlikely(p == NULL))
			return -ENOSPC;

		length = be32_to_cpup(p);
		p = xdr_inline_decode(xdr, length);
		if (unlikely(p == NULL))
			return -ENOSPC;

		if (length == sizeof(CREDS_VALUE) &&
		    memcmp(p, CREDS_VALUE, sizeof(CREDS_VALUE)) == 0) {
			/* We have creds here. parse them */
			err = gssx_dec_linux_creds(xdr, creds);
			if (err)
				return err;
			oa->data[0].value.len = 1; /* presence */
		} else {
			/* consume uninteresting buffer */
			err = gssx_dec_buffer(xdr, &dummy);
			if (err)
				return err;
		}
	}
	return 0;
}

static int gssx_dec_status(struct xdr_stream *xdr,
			   struct gssx_status *status)
{
	__be32 *p;
	int err;

	/* status->major_status */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(p == NULL))
		return -ENOSPC;
	p = xdr_decode_hyper(p, &status->major_status);

	/* status->mech */
	err = gssx_dec_buffer(xdr, &status->mech);
	if (err)
		return err;

	/* status->minor_status */
	p = xdr_inline_decode(xdr, 8);
	if (unlikely(p == NULL))
		return -ENOSPC;
	p = xdr_decode_hyper(p, &status->minor_status);

	/* status->major_status_string */
	err = gssx_dec_buffer(xdr, &status->major_status_string);
	if (err)
		return err;

	/* status->minor_status_string */
	err = gssx_dec_buffer(xdr, &status->minor_status_string);
	if (err)
		return err;

	/* status->server_ctx */
	err = gssx_dec_buffer(xdr, &status->server_ctx);
	if (err)
		return err;

	/* we assume we have no options for now, so simply consume them */
	/* status->options */
	err = dummy_dec_opt_array(xdr, &status->options);

	return err;
}

static int gssx_enc_call_ctx(struct xdr_stream *xdr,
			     struct gssx_call_ctx *ctx)
{
	struct gssx_option opt;
	__be32 *p;
	int err;

	/* ctx->locale */
	err = gssx_enc_buffer(xdr, &ctx->locale);
	if (err)
		return err;

	/* ctx->server_ctx */
	err = gssx_enc_buffer(xdr, &ctx->server_ctx);
	if (err)
		return err;

	/* we always want to ask for lucid contexts */
	/* ctx->options */
	p = xdr_reserve_space(xdr, 4);
	*p = cpu_to_be32(2);

	/* we want a lucid_v1 context */
	opt.option.data = LUCID_OPTION;
	opt.option.len = sizeof(LUCID_OPTION);
	opt.value.data = LUCID_VALUE;
	opt.value.len = sizeof(LUCID_VALUE);
	err = gssx_enc_option(xdr, &opt);

	/* ..and user creds */
	opt.option.data = CREDS_OPTION;
	opt.option.len = sizeof(CREDS_OPTION);
	opt.value.data = CREDS_VALUE;
	opt.value.len = sizeof(CREDS_VALUE);
	err = gssx_enc_option(xdr, &opt);

	return err;
}

static int gssx_dec_name_attr(struct xdr_stream *xdr,
			     struct gssx_name_attr *attr)
{
	int err;

	/* attr->attr */
	err = gssx_dec_buffer(xdr, &attr->attr);
	if (err)
		return err;

	/* attr->value */
	err = gssx_dec_buffer(xdr, &attr->value);
	if (err)
		return err;

	/* attr->extensions */
	err = dummy_dec_opt_array(xdr, &attr->extensions);

	return err;
}

static int dummy_enc_nameattr_array(struct xdr_stream *xdr,
				    struct gssx_name_attr_array *naa)
{
	__be32 *p;

	if (naa->count != 0)
		return -EINVAL;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return -ENOSPC;
	*p = 0;

	return 0;
}

static int dummy_dec_nameattr_array(struct xdr_stream *xdr,
				    struct gssx_name_attr_array *naa)
{
	struct gssx_name_attr dummy = { .attr = {.len = 0} };
	u32 count, i;
	__be32 *p;

	p = xdr_inline_decode(xdr, 4);
	if (unlikely(p == NULL))
		return -ENOSPC;
	count = be32_to_cpup(p++);
	for (i = 0; i < count; i++) {
		gssx_dec_name_attr(xdr, &dummy);
	}

	naa->count = 0;
	naa->data = NULL;
	return 0;
}

static struct xdr_netobj zero_netobj = {};

static struct gssx_name_attr_array zero_name_attr_array = {};

static struct gssx_option_array zero_option_array = {};

static int gssx_enc_name(struct xdr_stream *xdr,
			 struct gssx_name *name)
{
	int err;

	/* name->display_name */
	err = gssx_enc_buffer(xdr, &name->display_name);
	if (err)
		return err;

	/* name->name_type */
	err = gssx_enc_buffer(xdr, &zero_netobj);
	if (err)
		return err;

	/* name->exported_name */
	err = gssx_enc_buffer(xdr, &zero_netobj);
	if (err)
		return err;

	/* name->exported_composite_name */
	err = gssx_enc_buffer(xdr, &zero_netobj);
	if (err)
		return err;

	/* leave name_attributes empty for now, will add once we have any
	 * to pass up at all */
	/* name->name_attributes */
	err = dummy_enc_nameattr_array(xdr, &zero_name_attr_array);
	if (err)
		return err;

	/* leave options empty for now, will add once we have any options
	 * to pass up at all */
	/* name->extensions */
	err = dummy_enc_opt_array(xdr, &zero_option_array);

	return err;
}


static int gssx_dec_name(struct xdr_stream *xdr,
			 struct gssx_name *name)
{
	struct xdr_netobj dummy_netobj = { .len = 0 };
	struct gssx_name_attr_array dummy_name_attr_array = { .count = 0 };
	struct gssx_option_array dummy_option_array = { .count = 0 };
	int err;

	/* name->display_name */
	err = gssx_dec_buffer(xdr, &name->display_name);
	if (err)
		return err;

	/* name->name_type */
	err = gssx_dec_buffer(xdr, &dummy_netobj);
	if (err)
		return err;

	/* name->exported_name */
	err = gssx_dec_buffer(xdr, &dummy_netobj);
	if (err)
		return err;

	/* name->exported_composite_name */
	err = gssx_dec_buffer(xdr, &dummy_netobj);
	if (err)
		return err;

	/* we assume we have no attributes for now, so simply consume them */
	/* name->name_attributes */
	err = dummy_dec_nameattr_array(xdr, &dummy_name_attr_array);
	if (err)
		return err;

	/* we assume we have no options for now, so simply consume them */
	/* name->extensions */
	err = dummy_dec_opt_array(xdr, &dummy_option_array);

	return err;
}

static int dummy_enc_credel_array(struct xdr_stream *xdr,
				  struct gssx_cred_element_array *cea)
{
	__be32 *p;

	if (cea->count != 0)
		return -EINVAL;

	p = xdr_reserve_space(xdr, 4);
	if (!p)
		return -ENOSPC;
	*p = 0;

	return 0;
}

static int gssx_enc_cred(struct xdr_stream *xdr,
			 struct gssx_cred *cred)
{
	int err;

	/* cred->desired_name */
	err = gssx_enc_name(xdr, &cred->desired_name);
	if (err)
		return err;

	/* cred->elements */
	err = dummy_enc_credel_array(xdr, &cred->elements);
	if (err)
		return err;

	/* cred->cred_handle_reference */
	err = gssx_enc_buffer(xdr, &cred->cred_handle_reference);
	if (err)
		return err;

	/* cred->needs_release */
	err = gssx_enc_bool(xdr, cred->needs_release);

	return err;
}

static int gssx_enc_ctx(struct xdr_stream *xdr,
			struct gssx_ctx *ctx)
{
	__be32 *p;
	int err;

	/* ctx->exported_context_token */
	err = gssx_enc_buffer(xdr, &ctx->exported_context_token);
	if (err)
		return err;

	/* ctx->state */
	err = gssx_enc_buffer(xdr, &ctx->state);
	if (err)
		return err;

	/* ctx->need_release */
	err = gssx_enc_bool(xdr, ctx->need_release);
	if (err)
		return err;

	/* ctx->mech */
	err = gssx_enc_buffer(xdr, &ctx->mech);
	if (err)
		return err;

	/* ctx->src_name */
	err = gssx_enc_name(xdr, &ctx->src_name);
	if (err)
		return err;

	/* ctx->targ_name */
	err = gssx_enc_name(xdr, &ctx->targ_name);
	if (err)
		return err;

	/* ctx->lifetime */
	p = xdr_reserve_space(xdr, 8+8);
	if (!p)
		return -ENOSPC;
	p = xdr_encode_hyper(p, ctx->lifetime);

	/* ctx->ctx_flags */
	p = xdr_encode_hyper(p, ctx->ctx_flags);

	/* ctx->locally_initiated */
	err = gssx_enc_bool(xdr, ctx->locally_initiated);
	if (err)
		return err;

	/* ctx->open */
	err = gssx_enc_bool(xdr, ctx->open);
	if (err)
		return err;

	/* leave options empty for now, will add once we have any options
	 * to pass up at all */
	/* ctx->options */
	err = dummy_enc_opt_array(xdr, &ctx->options);

	return err;
}

static int gssx_dec_ctx(struct xdr_stream *xdr,
			struct gssx_ctx *ctx)
{
	__be32 *p;
	int err;

	/* ctx->exported_context_token */
	err = gssx_dec_buffer(xdr, &ctx->exported_context_token);
	if (err)
		return err;

	/* ctx->state */
	err = gssx_dec_buffer(xdr, &ctx->state);
	if (err)
		return err;

	/* ctx->need_release */
	err = gssx_dec_bool(xdr, &ctx->need_release);
	if (err)
		return err;

	/* ctx->mech */
	err = gssx_dec_buffer(xdr, &ctx->mech);
	if (err)
		return err;

	/* ctx->src_name */
	err = gssx_dec_name(xdr, &ctx->src_name);
	if (err)
		return err;

	/* ctx->targ_name */
	err = gssx_dec_name(xdr, &ctx->targ_name);
	if (err)
		return err;

	/* ctx->lifetime */
	p = xdr_inline_decode(xdr, 8+8);
	if (unlikely(p == NULL))
		return -ENOSPC;
	p = xdr_decode_hyper(p, &ctx->lifetime);

	/* ctx->ctx_flags */
	p = xdr_decode_hyper(p, &ctx->ctx_flags);

	/* ctx->locally_initiated */
	err = gssx_dec_bool(xdr, &ctx->locally_initiated);
	if (err)
		return err;

	/* ctx->open */
	err = gssx_dec_bool(xdr, &ctx->open);
	if (err)
		return err;

	/* we assume we have no options for now, so simply consume them */
	/* ctx->options */
	err = dummy_dec_opt_array(xdr, &ctx->options);

	return err;
}

static int gssx_enc_cb(struct xdr_stream *xdr, struct gssx_cb *cb)
{
	__be32 *p;
	int err;

	/* cb->initiator_addrtype */
	p = xdr_reserve_space(xdr, 8);
	if (!p)
		return -ENOSPC;
	p = xdr_encode_hyper(p, cb->initiator_addrtype);

	/* cb->initiator_address */
	err = gssx_enc_buffer(xdr, &cb->initiator_address);
	if (err)
		return err;

	/* cb->acceptor_addrtype */
	p = xdr_reserve_space(xdr, 8);
	if (!p)
		return -ENOSPC;
	p = xdr_encode_hyper(p, cb->acceptor_addrtype);

	/* cb->acceptor_address */
	err = gssx_enc_buffer(xdr, &cb->acceptor_address);
	if (err)
		return err;

	/* cb->application_data */
	err = gssx_enc_buffer(xdr, &cb->application_data);

	return err;
}

void gssx_enc_accept_sec_context(struct rpc_rqst *req,
				 struct xdr_stream *xdr,
				 struct gssx_arg_accept_sec_context *arg)
{
	int err;

	err = gssx_enc_call_ctx(xdr, &arg->call_ctx);
	if (err)
		goto done;

	/* arg->context_handle */
	if (arg->context_handle)
		err = gssx_enc_ctx(xdr, arg->context_handle);
	else
		err = gssx_enc_bool(xdr, 0);
	if (err)
		goto done;

	/* arg->cred_handle */
	if (arg->cred_handle)
		err = gssx_enc_cred(xdr, arg->cred_handle);
	else
		err = gssx_enc_bool(xdr, 0);
	if (err)
		goto done;

	/* arg->input_token */
	err = gssx_enc_in_token(xdr, &arg->input_token);
	if (err)
		goto done;

	/* arg->input_cb */
	if (arg->input_cb)
		err = gssx_enc_cb(xdr, arg->input_cb);
	else
		err = gssx_enc_bool(xdr, 0);
	if (err)
		goto done;

	err = gssx_enc_bool(xdr, arg->ret_deleg_cred);
	if (err)
		goto done;

	/* leave options empty for now, will add once we have any options
	 * to pass up at all */
	/* arg->options */
	err = dummy_enc_opt_array(xdr, &arg->options);

	xdr_inline_pages(&req->rq_rcv_buf,
		PAGE_SIZE/2 /* pretty arbitrary */,
		arg->pages, 0 /* page base */, arg->npages * PAGE_SIZE);
done:
	if (err)
		dprintk("RPC:       gssx_enc_accept_sec_context: %d\n", err);
}

int gssx_dec_accept_sec_context(struct rpc_rqst *rqstp,
				struct xdr_stream *xdr,
				struct gssx_res_accept_sec_context *res)
{
	u32 value_follows;
	int err;
	struct page *scratch;

	scratch = alloc_page(GFP_KERNEL);
	if (!scratch)
		return -ENOMEM;
	xdr_set_scratch_buffer(xdr, page_address(scratch), PAGE_SIZE);

	/* res->status */
	err = gssx_dec_status(xdr, &res->status);
	if (err)
		goto out_free;

	/* res->context_handle */
	err = gssx_dec_bool(xdr, &value_follows);
	if (err)
		goto out_free;
	if (value_follows) {
		err = gssx_dec_ctx(xdr, res->context_handle);
		if (err)
			goto out_free;
	} else {
		res->context_handle = NULL;
	}

	/* res->output_token */
	err = gssx_dec_bool(xdr, &value_follows);
	if (err)
		goto out_free;
	if (value_follows) {
		err = gssx_dec_buffer(xdr, res->output_token);
		if (err)
			goto out_free;
	} else {
		res->output_token = NULL;
	}

	/* res->delegated_cred_handle */
	err = gssx_dec_bool(xdr, &value_follows);
	if (err)
		goto out_free;
	if (value_follows) {
		/* we do not support upcall servers sending this data. */
		err = -EINVAL;
		goto out_free;
	}

	/* res->options */
	err = gssx_dec_option_array(xdr, &res->options);

out_free:
	__free_page(scratch);
	return err;
}
