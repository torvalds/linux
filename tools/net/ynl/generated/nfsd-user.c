// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/nfsd.yaml */
/* YNL-GEN user source */

#include <stdlib.h>
#include <string.h>
#include "nfsd-user.h"
#include "ynl.h"
#include <linux/nfsd_netlink.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

/* Enums */
static const char * const nfsd_op_strmap[] = {
	[NFSD_CMD_RPC_STATUS_GET] = "rpc-status-get",
};

const char *nfsd_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(nfsd_op_strmap))
		return NULL;
	return nfsd_op_strmap[op];
}

/* Policies */
struct ynl_policy_attr nfsd_rpc_status_policy[NFSD_A_RPC_STATUS_MAX + 1] = {
	[NFSD_A_RPC_STATUS_XID] = { .name = "xid", .type = YNL_PT_U32, },
	[NFSD_A_RPC_STATUS_FLAGS] = { .name = "flags", .type = YNL_PT_U32, },
	[NFSD_A_RPC_STATUS_PROG] = { .name = "prog", .type = YNL_PT_U32, },
	[NFSD_A_RPC_STATUS_VERSION] = { .name = "version", .type = YNL_PT_U8, },
	[NFSD_A_RPC_STATUS_PROC] = { .name = "proc", .type = YNL_PT_U32, },
	[NFSD_A_RPC_STATUS_SERVICE_TIME] = { .name = "service_time", .type = YNL_PT_U64, },
	[NFSD_A_RPC_STATUS_PAD] = { .name = "pad", .type = YNL_PT_IGNORE, },
	[NFSD_A_RPC_STATUS_SADDR4] = { .name = "saddr4", .type = YNL_PT_U32, },
	[NFSD_A_RPC_STATUS_DADDR4] = { .name = "daddr4", .type = YNL_PT_U32, },
	[NFSD_A_RPC_STATUS_SADDR6] = { .name = "saddr6", .type = YNL_PT_BINARY,},
	[NFSD_A_RPC_STATUS_DADDR6] = { .name = "daddr6", .type = YNL_PT_BINARY,},
	[NFSD_A_RPC_STATUS_SPORT] = { .name = "sport", .type = YNL_PT_U16, },
	[NFSD_A_RPC_STATUS_DPORT] = { .name = "dport", .type = YNL_PT_U16, },
	[NFSD_A_RPC_STATUS_COMPOUND_OPS] = { .name = "compound-ops", .type = YNL_PT_U32, },
};

struct ynl_policy_nest nfsd_rpc_status_nest = {
	.max_attr = NFSD_A_RPC_STATUS_MAX,
	.table = nfsd_rpc_status_policy,
};

/* Common nested types */
/* ============== NFSD_CMD_RPC_STATUS_GET ============== */
/* NFSD_CMD_RPC_STATUS_GET - dump */
int nfsd_rpc_status_get_rsp_dump_parse(const struct nlmsghdr *nlh, void *data)
{
	struct nfsd_rpc_status_get_rsp_dump *dst;
	struct ynl_parse_arg *yarg = data;
	unsigned int n_compound_ops = 0;
	const struct nlattr *attr;
	int i;

	dst = yarg->data;

	if (dst->compound_ops)
		return ynl_error_parse(yarg, "attribute already present (rpc-status.compound-ops)");

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == NFSD_A_RPC_STATUS_XID) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.xid = 1;
			dst->xid = mnl_attr_get_u32(attr);
		} else if (type == NFSD_A_RPC_STATUS_FLAGS) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.flags = 1;
			dst->flags = mnl_attr_get_u32(attr);
		} else if (type == NFSD_A_RPC_STATUS_PROG) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.prog = 1;
			dst->prog = mnl_attr_get_u32(attr);
		} else if (type == NFSD_A_RPC_STATUS_VERSION) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.version = 1;
			dst->version = mnl_attr_get_u8(attr);
		} else if (type == NFSD_A_RPC_STATUS_PROC) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.proc = 1;
			dst->proc = mnl_attr_get_u32(attr);
		} else if (type == NFSD_A_RPC_STATUS_SERVICE_TIME) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.service_time = 1;
			dst->service_time = mnl_attr_get_u64(attr);
		} else if (type == NFSD_A_RPC_STATUS_SADDR4) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.saddr4 = 1;
			dst->saddr4 = mnl_attr_get_u32(attr);
		} else if (type == NFSD_A_RPC_STATUS_DADDR4) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.daddr4 = 1;
			dst->daddr4 = mnl_attr_get_u32(attr);
		} else if (type == NFSD_A_RPC_STATUS_SADDR6) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.saddr6_len = len;
			dst->saddr6 = malloc(len);
			memcpy(dst->saddr6, mnl_attr_get_payload(attr), len);
		} else if (type == NFSD_A_RPC_STATUS_DADDR6) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.daddr6_len = len;
			dst->daddr6 = malloc(len);
			memcpy(dst->daddr6, mnl_attr_get_payload(attr), len);
		} else if (type == NFSD_A_RPC_STATUS_SPORT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.sport = 1;
			dst->sport = mnl_attr_get_u16(attr);
		} else if (type == NFSD_A_RPC_STATUS_DPORT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.dport = 1;
			dst->dport = mnl_attr_get_u16(attr);
		} else if (type == NFSD_A_RPC_STATUS_COMPOUND_OPS) {
			n_compound_ops++;
		}
	}

	if (n_compound_ops) {
		dst->compound_ops = calloc(n_compound_ops, sizeof(*dst->compound_ops));
		dst->n_compound_ops = n_compound_ops;
		i = 0;
		mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
			if (mnl_attr_get_type(attr) == NFSD_A_RPC_STATUS_COMPOUND_OPS) {
				dst->compound_ops[i] = mnl_attr_get_u32(attr);
				i++;
			}
		}
	}

	return MNL_CB_OK;
}

void
nfsd_rpc_status_get_rsp_list_free(struct nfsd_rpc_status_get_rsp_list *rsp)
{
	struct nfsd_rpc_status_get_rsp_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.saddr6);
		free(rsp->obj.daddr6);
		free(rsp->obj.compound_ops);
		free(rsp);
	}
}

struct nfsd_rpc_status_get_rsp_list *
nfsd_rpc_status_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct nfsd_rpc_status_get_rsp_list);
	yds.cb = nfsd_rpc_status_get_rsp_dump_parse;
	yds.rsp_cmd = NFSD_CMD_RPC_STATUS_GET;
	yds.rsp_policy = &nfsd_rpc_status_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, NFSD_CMD_RPC_STATUS_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	nfsd_rpc_status_get_rsp_list_free(yds.first);
	return NULL;
}

const struct ynl_family ynl_nfsd_family =  {
	.name		= "nfsd",
};
