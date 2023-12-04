// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/fou.yaml */
/* YNL-GEN user source */

#include <stdlib.h>
#include <string.h>
#include "fou-user.h"
#include "ynl.h"
#include <linux/fou.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

/* Enums */
static const char * const fou_op_strmap[] = {
	[FOU_CMD_ADD] = "add",
	[FOU_CMD_DEL] = "del",
	[FOU_CMD_GET] = "get",
};

const char *fou_op_str(int op)
{
	if (op < 0 || op >= (int)MNL_ARRAY_SIZE(fou_op_strmap))
		return NULL;
	return fou_op_strmap[op];
}

static const char * const fou_encap_type_strmap[] = {
	[0] = "unspec",
	[1] = "direct",
	[2] = "gue",
};

const char *fou_encap_type_str(int value)
{
	if (value < 0 || value >= (int)MNL_ARRAY_SIZE(fou_encap_type_strmap))
		return NULL;
	return fou_encap_type_strmap[value];
}

/* Policies */
struct ynl_policy_attr fou_policy[FOU_ATTR_MAX + 1] = {
	[FOU_ATTR_UNSPEC] = { .name = "unspec", .type = YNL_PT_REJECT, },
	[FOU_ATTR_PORT] = { .name = "port", .type = YNL_PT_U16, },
	[FOU_ATTR_AF] = { .name = "af", .type = YNL_PT_U8, },
	[FOU_ATTR_IPPROTO] = { .name = "ipproto", .type = YNL_PT_U8, },
	[FOU_ATTR_TYPE] = { .name = "type", .type = YNL_PT_U8, },
	[FOU_ATTR_REMCSUM_NOPARTIAL] = { .name = "remcsum_nopartial", .type = YNL_PT_FLAG, },
	[FOU_ATTR_LOCAL_V4] = { .name = "local_v4", .type = YNL_PT_U32, },
	[FOU_ATTR_LOCAL_V6] = { .name = "local_v6", .type = YNL_PT_BINARY,},
	[FOU_ATTR_PEER_V4] = { .name = "peer_v4", .type = YNL_PT_U32, },
	[FOU_ATTR_PEER_V6] = { .name = "peer_v6", .type = YNL_PT_BINARY,},
	[FOU_ATTR_PEER_PORT] = { .name = "peer_port", .type = YNL_PT_U16, },
	[FOU_ATTR_IFINDEX] = { .name = "ifindex", .type = YNL_PT_U32, },
};

struct ynl_policy_nest fou_nest = {
	.max_attr = FOU_ATTR_MAX,
	.table = fou_policy,
};

/* Common nested types */
/* ============== FOU_CMD_ADD ============== */
/* FOU_CMD_ADD - do */
void fou_add_req_free(struct fou_add_req *req)
{
	free(req->local_v6);
	free(req->peer_v6);
	free(req);
}

int fou_add(struct ynl_sock *ys, struct fou_add_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, FOU_CMD_ADD, 1);
	ys->req_policy = &fou_nest;

	if (req->_present.port)
		mnl_attr_put_u16(nlh, FOU_ATTR_PORT, req->port);
	if (req->_present.ipproto)
		mnl_attr_put_u8(nlh, FOU_ATTR_IPPROTO, req->ipproto);
	if (req->_present.type)
		mnl_attr_put_u8(nlh, FOU_ATTR_TYPE, req->type);
	if (req->_present.remcsum_nopartial)
		mnl_attr_put(nlh, FOU_ATTR_REMCSUM_NOPARTIAL, 0, NULL);
	if (req->_present.local_v4)
		mnl_attr_put_u32(nlh, FOU_ATTR_LOCAL_V4, req->local_v4);
	if (req->_present.peer_v4)
		mnl_attr_put_u32(nlh, FOU_ATTR_PEER_V4, req->peer_v4);
	if (req->_present.local_v6_len)
		mnl_attr_put(nlh, FOU_ATTR_LOCAL_V6, req->_present.local_v6_len, req->local_v6);
	if (req->_present.peer_v6_len)
		mnl_attr_put(nlh, FOU_ATTR_PEER_V6, req->_present.peer_v6_len, req->peer_v6);
	if (req->_present.peer_port)
		mnl_attr_put_u16(nlh, FOU_ATTR_PEER_PORT, req->peer_port);
	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, FOU_ATTR_IFINDEX, req->ifindex);

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== FOU_CMD_DEL ============== */
/* FOU_CMD_DEL - do */
void fou_del_req_free(struct fou_del_req *req)
{
	free(req->local_v6);
	free(req->peer_v6);
	free(req);
}

int fou_del(struct ynl_sock *ys, struct fou_del_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, FOU_CMD_DEL, 1);
	ys->req_policy = &fou_nest;

	if (req->_present.af)
		mnl_attr_put_u8(nlh, FOU_ATTR_AF, req->af);
	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, FOU_ATTR_IFINDEX, req->ifindex);
	if (req->_present.port)
		mnl_attr_put_u16(nlh, FOU_ATTR_PORT, req->port);
	if (req->_present.peer_port)
		mnl_attr_put_u16(nlh, FOU_ATTR_PEER_PORT, req->peer_port);
	if (req->_present.local_v4)
		mnl_attr_put_u32(nlh, FOU_ATTR_LOCAL_V4, req->local_v4);
	if (req->_present.peer_v4)
		mnl_attr_put_u32(nlh, FOU_ATTR_PEER_V4, req->peer_v4);
	if (req->_present.local_v6_len)
		mnl_attr_put(nlh, FOU_ATTR_LOCAL_V6, req->_present.local_v6_len, req->local_v6);
	if (req->_present.peer_v6_len)
		mnl_attr_put(nlh, FOU_ATTR_PEER_V6, req->_present.peer_v6_len, req->peer_v6);

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		return -1;

	return 0;
}

/* ============== FOU_CMD_GET ============== */
/* FOU_CMD_GET - do */
void fou_get_req_free(struct fou_get_req *req)
{
	free(req->local_v6);
	free(req->peer_v6);
	free(req);
}

void fou_get_rsp_free(struct fou_get_rsp *rsp)
{
	free(rsp->local_v6);
	free(rsp->peer_v6);
	free(rsp);
}

int fou_get_rsp_parse(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	const struct nlattr *attr;
	struct fou_get_rsp *dst;

	dst = yarg->data;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		unsigned int type = mnl_attr_get_type(attr);

		if (type == FOU_ATTR_PORT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.port = 1;
			dst->port = mnl_attr_get_u16(attr);
		} else if (type == FOU_ATTR_IPPROTO) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ipproto = 1;
			dst->ipproto = mnl_attr_get_u8(attr);
		} else if (type == FOU_ATTR_TYPE) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.type = 1;
			dst->type = mnl_attr_get_u8(attr);
		} else if (type == FOU_ATTR_REMCSUM_NOPARTIAL) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.remcsum_nopartial = 1;
		} else if (type == FOU_ATTR_LOCAL_V4) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.local_v4 = 1;
			dst->local_v4 = mnl_attr_get_u32(attr);
		} else if (type == FOU_ATTR_PEER_V4) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.peer_v4 = 1;
			dst->peer_v4 = mnl_attr_get_u32(attr);
		} else if (type == FOU_ATTR_LOCAL_V6) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.local_v6_len = len;
			dst->local_v6 = malloc(len);
			memcpy(dst->local_v6, mnl_attr_get_payload(attr), len);
		} else if (type == FOU_ATTR_PEER_V6) {
			unsigned int len;

			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;

			len = mnl_attr_get_payload_len(attr);
			dst->_present.peer_v6_len = len;
			dst->peer_v6 = malloc(len);
			memcpy(dst->peer_v6, mnl_attr_get_payload(attr), len);
		} else if (type == FOU_ATTR_PEER_PORT) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.peer_port = 1;
			dst->peer_port = mnl_attr_get_u16(attr);
		} else if (type == FOU_ATTR_IFINDEX) {
			if (ynl_attr_validate(yarg, attr))
				return MNL_CB_ERROR;
			dst->_present.ifindex = 1;
			dst->ifindex = mnl_attr_get_u32(attr);
		}
	}

	return MNL_CB_OK;
}

struct fou_get_rsp *fou_get(struct ynl_sock *ys, struct fou_get_req *req)
{
	struct ynl_req_state yrs = { .yarg = { .ys = ys, }, };
	struct fou_get_rsp *rsp;
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, ys->family_id, FOU_CMD_GET, 1);
	ys->req_policy = &fou_nest;
	yrs.yarg.rsp_policy = &fou_nest;

	if (req->_present.af)
		mnl_attr_put_u8(nlh, FOU_ATTR_AF, req->af);
	if (req->_present.ifindex)
		mnl_attr_put_u32(nlh, FOU_ATTR_IFINDEX, req->ifindex);
	if (req->_present.port)
		mnl_attr_put_u16(nlh, FOU_ATTR_PORT, req->port);
	if (req->_present.peer_port)
		mnl_attr_put_u16(nlh, FOU_ATTR_PEER_PORT, req->peer_port);
	if (req->_present.local_v4)
		mnl_attr_put_u32(nlh, FOU_ATTR_LOCAL_V4, req->local_v4);
	if (req->_present.peer_v4)
		mnl_attr_put_u32(nlh, FOU_ATTR_PEER_V4, req->peer_v4);
	if (req->_present.local_v6_len)
		mnl_attr_put(nlh, FOU_ATTR_LOCAL_V6, req->_present.local_v6_len, req->local_v6);
	if (req->_present.peer_v6_len)
		mnl_attr_put(nlh, FOU_ATTR_PEER_V6, req->_present.peer_v6_len, req->peer_v6);

	rsp = calloc(1, sizeof(*rsp));
	yrs.yarg.data = rsp;
	yrs.cb = fou_get_rsp_parse;
	yrs.rsp_cmd = FOU_CMD_GET;

	err = ynl_exec(ys, nlh, &yrs);
	if (err < 0)
		goto err_free;

	return rsp;

err_free:
	fou_get_rsp_free(rsp);
	return NULL;
}

/* FOU_CMD_GET - dump */
void fou_get_list_free(struct fou_get_list *rsp)
{
	struct fou_get_list *next = rsp;

	while ((void *)next != YNL_LIST_END) {
		rsp = next;
		next = rsp->next;

		free(rsp->obj.local_v6);
		free(rsp->obj.peer_v6);
		free(rsp);
	}
}

struct fou_get_list *fou_get_dump(struct ynl_sock *ys)
{
	struct ynl_dump_state yds = {};
	struct nlmsghdr *nlh;
	int err;

	yds.ys = ys;
	yds.alloc_sz = sizeof(struct fou_get_list);
	yds.cb = fou_get_rsp_parse;
	yds.rsp_cmd = FOU_CMD_GET;
	yds.rsp_policy = &fou_nest;

	nlh = ynl_gemsg_start_dump(ys, ys->family_id, FOU_CMD_GET, 1);

	err = ynl_exec_dump(ys, nlh, &yds);
	if (err < 0)
		goto free_list;

	return yds.first;

free_list:
	fou_get_list_free(yds.first);
	return NULL;
}

const struct ynl_family ynl_fou_family =  {
	.name		= "fou",
};
