// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <linux/types.h>

#include <libmnl/libmnl.h>
#include <linux/genetlink.h>

#include "ynl.h"

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof(*arr))

#define __yerr_msg(yse, _msg...)					\
	({								\
		struct ynl_error *_yse = (yse);				\
									\
		if (_yse) {						\
			snprintf(_yse->msg, sizeof(_yse->msg) - 1,  _msg); \
			_yse->msg[sizeof(_yse->msg) - 1] = 0;		\
		}							\
	})

#define __yerr_code(yse, _code...)		\
	({					\
		struct ynl_error *_yse = (yse);	\
						\
		if (_yse) {			\
			_yse->code = _code;	\
		}				\
	})

#define __yerr(yse, _code, _msg...)		\
	({					\
		__yerr_msg(yse, _msg);		\
		__yerr_code(yse, _code);	\
	})

#define __perr(yse, _msg)		__yerr(yse, errno, _msg)

#define yerr_msg(_ys, _msg...)		__yerr_msg(&(_ys)->err, _msg)
#define yerr(_ys, _code, _msg...)	__yerr(&(_ys)->err, _code, _msg)
#define perr(_ys, _msg)			__yerr(&(_ys)->err, errno, _msg)

/* -- Netlink boiler plate */
static int
ynl_err_walk_report_one(struct ynl_policy_nest *policy, unsigned int type,
			char *str, int str_sz, int *n)
{
	if (!policy) {
		if (*n < str_sz)
			*n += snprintf(str, str_sz, "!policy");
		return 1;
	}

	if (type > policy->max_attr) {
		if (*n < str_sz)
			*n += snprintf(str, str_sz, "!oob");
		return 1;
	}

	if (!policy->table[type].name) {
		if (*n < str_sz)
			*n += snprintf(str, str_sz, "!name");
		return 1;
	}

	if (*n < str_sz)
		*n += snprintf(str, str_sz - *n,
			       ".%s", policy->table[type].name);
	return 0;
}

static int
ynl_err_walk(struct ynl_sock *ys, void *start, void *end, unsigned int off,
	     struct ynl_policy_nest *policy, char *str, int str_sz,
	     struct ynl_policy_nest **nest_pol)
{
	unsigned int astart_off, aend_off;
	const struct nlattr *attr;
	unsigned int data_len;
	unsigned int type;
	bool found = false;
	int n = 0;

	if (!policy) {
		if (n < str_sz)
			n += snprintf(str, str_sz, "!policy");
		return n;
	}

	data_len = end - start;

	mnl_attr_for_each_payload(start, data_len) {
		astart_off = (char *)attr - (char *)start;
		aend_off = astart_off + mnl_attr_get_payload_len(attr);
		if (aend_off <= off)
			continue;

		found = true;
		break;
	}
	if (!found)
		return 0;

	off -= astart_off;

	type = mnl_attr_get_type(attr);

	if (ynl_err_walk_report_one(policy, type, str, str_sz, &n))
		return n;

	if (!off) {
		if (nest_pol)
			*nest_pol = policy->table[type].nest;
		return n;
	}

	if (!policy->table[type].nest) {
		if (n < str_sz)
			n += snprintf(str, str_sz, "!nest");
		return n;
	}

	off -= sizeof(struct nlattr);
	start =  mnl_attr_get_payload(attr);
	end = start + mnl_attr_get_payload_len(attr);

	return n + ynl_err_walk(ys, start, end, off, policy->table[type].nest,
				&str[n], str_sz - n, nest_pol);
}

#define NLMSGERR_ATTR_MISS_TYPE (NLMSGERR_ATTR_POLICY + 1)
#define NLMSGERR_ATTR_MISS_NEST (NLMSGERR_ATTR_POLICY + 2)
#define NLMSGERR_ATTR_MAX (NLMSGERR_ATTR_MAX + 2)

static int
ynl_ext_ack_check(struct ynl_sock *ys, const struct nlmsghdr *nlh,
		  unsigned int hlen)
{
	const struct nlattr *tb[NLMSGERR_ATTR_MAX + 1] = {};
	char miss_attr[sizeof(ys->err.msg)];
	char bad_attr[sizeof(ys->err.msg)];
	const struct nlattr *attr;
	const char *str = NULL;

	if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS)) {
		yerr_msg(ys, "%s", strerror(ys->err.code));
		return MNL_CB_OK;
	}

	mnl_attr_for_each(attr, nlh, hlen) {
		unsigned int len, type;

		len = mnl_attr_get_payload_len(attr);
		type = mnl_attr_get_type(attr);

		if (type > NLMSGERR_ATTR_MAX)
			continue;

		tb[type] = attr;

		switch (type) {
		case NLMSGERR_ATTR_OFFS:
		case NLMSGERR_ATTR_MISS_TYPE:
		case NLMSGERR_ATTR_MISS_NEST:
			if (len != sizeof(__u32))
				return MNL_CB_ERROR;
			break;
		case NLMSGERR_ATTR_MSG:
			str = mnl_attr_get_payload(attr);
			if (str[len - 1])
				return MNL_CB_ERROR;
			break;
		default:
			break;
		}
	}

	bad_attr[0] = '\0';
	miss_attr[0] = '\0';

	if (tb[NLMSGERR_ATTR_OFFS]) {
		unsigned int n, off;
		void *start, *end;

		ys->err.attr_offs = mnl_attr_get_u32(tb[NLMSGERR_ATTR_OFFS]);

		n = snprintf(bad_attr, sizeof(bad_attr), "%sbad attribute: ",
			     str ? " (" : "");

		start = mnl_nlmsg_get_payload_offset(ys->nlh,
						     ys->family->hdr_len);
		end = mnl_nlmsg_get_payload_tail(ys->nlh);

		off = ys->err.attr_offs;
		off -= sizeof(struct nlmsghdr);
		off -= ys->family->hdr_len;

		n += ynl_err_walk(ys, start, end, off, ys->req_policy,
				  &bad_attr[n], sizeof(bad_attr) - n, NULL);

		if (n >= sizeof(bad_attr))
			n = sizeof(bad_attr) - 1;
		bad_attr[n] = '\0';
	}
	if (tb[NLMSGERR_ATTR_MISS_TYPE]) {
		struct ynl_policy_nest *nest_pol = NULL;
		unsigned int n, off, type;
		void *start, *end;
		int n2;

		type = mnl_attr_get_u32(tb[NLMSGERR_ATTR_MISS_TYPE]);

		n = snprintf(miss_attr, sizeof(miss_attr), "%smissing attribute: ",
			     bad_attr[0] ? ", " : (str ? " (" : ""));

		start = mnl_nlmsg_get_payload_offset(ys->nlh,
						     ys->family->hdr_len);
		end = mnl_nlmsg_get_payload_tail(ys->nlh);

		nest_pol = ys->req_policy;
		if (tb[NLMSGERR_ATTR_MISS_NEST]) {
			off = mnl_attr_get_u32(tb[NLMSGERR_ATTR_MISS_NEST]);
			off -= sizeof(struct nlmsghdr);
			off -= ys->family->hdr_len;

			n += ynl_err_walk(ys, start, end, off, ys->req_policy,
					  &miss_attr[n], sizeof(miss_attr) - n,
					  &nest_pol);
		}

		n2 = 0;
		ynl_err_walk_report_one(nest_pol, type, &miss_attr[n],
					sizeof(miss_attr) - n, &n2);
		n += n2;

		if (n >= sizeof(miss_attr))
			n = sizeof(miss_attr) - 1;
		miss_attr[n] = '\0';
	}

	/* Implicitly depend on ys->err.code already set */
	if (str)
		yerr_msg(ys, "Kernel %s: '%s'%s%s%s",
			 ys->err.code ? "error" : "warning",
			 str, bad_attr, miss_attr,
			 bad_attr[0] || miss_attr[0] ? ")" : "");
	else if (bad_attr[0] || miss_attr[0])
		yerr_msg(ys, "Kernel %s: %s%s",
			 ys->err.code ? "error" : "warning",
			 bad_attr, miss_attr);
	else
		yerr_msg(ys, "%s", strerror(ys->err.code));

	return MNL_CB_OK;
}

static int ynl_cb_error(const struct nlmsghdr *nlh, void *data)
{
	const struct nlmsgerr *err = mnl_nlmsg_get_payload(nlh);
	struct ynl_parse_arg *yarg = data;
	unsigned int hlen;
	int code;

	code = err->error >= 0 ? err->error : -err->error;
	yarg->ys->err.code = code;
	errno = code;

	hlen = sizeof(*err);
	if (!(nlh->nlmsg_flags & NLM_F_CAPPED))
		hlen += mnl_nlmsg_get_payload_len(&err->msg);

	ynl_ext_ack_check(yarg->ys, nlh, hlen);

	return code ? MNL_CB_ERROR : MNL_CB_STOP;
}

static int ynl_cb_done(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	int err;

	err = *(int *)NLMSG_DATA(nlh);
	if (err < 0) {
		yarg->ys->err.code = -err;
		errno = -err;

		ynl_ext_ack_check(yarg->ys, nlh, sizeof(int));

		return MNL_CB_ERROR;
	}
	return MNL_CB_STOP;
}

static int ynl_cb_noop(const struct nlmsghdr *nlh, void *data)
{
	return MNL_CB_OK;
}

mnl_cb_t ynl_cb_array[NLMSG_MIN_TYPE] = {
	[NLMSG_NOOP]	= ynl_cb_noop,
	[NLMSG_ERROR]	= ynl_cb_error,
	[NLMSG_DONE]	= ynl_cb_done,
	[NLMSG_OVERRUN]	= ynl_cb_noop,
};

/* Attribute validation */

int ynl_attr_validate(struct ynl_parse_arg *yarg, const struct nlattr *attr)
{
	struct ynl_policy_attr *policy;
	unsigned int type, len;
	unsigned char *data;

	data = mnl_attr_get_payload(attr);
	len = mnl_attr_get_payload_len(attr);
	type = mnl_attr_get_type(attr);
	if (type > yarg->rsp_policy->max_attr) {
		yerr(yarg->ys, YNL_ERROR_INTERNAL,
		     "Internal error, validating unknown attribute");
		return -1;
	}

	policy = &yarg->rsp_policy->table[type];

	switch (policy->type) {
	case YNL_PT_REJECT:
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Rejected attribute (%s)", policy->name);
		return -1;
	case YNL_PT_IGNORE:
		break;
	case YNL_PT_U8:
		if (len == sizeof(__u8))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (u8 %s)", policy->name);
		return -1;
	case YNL_PT_U16:
		if (len == sizeof(__u16))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (u16 %s)", policy->name);
		return -1;
	case YNL_PT_U32:
		if (len == sizeof(__u32))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (u32 %s)", policy->name);
		return -1;
	case YNL_PT_U64:
		if (len == sizeof(__u64))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (u64 %s)", policy->name);
		return -1;
	case YNL_PT_UINT:
		if (len == sizeof(__u32) || len == sizeof(__u64))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (uint %s)", policy->name);
		return -1;
	case YNL_PT_FLAG:
		/* Let flags grow into real attrs, why not.. */
		break;
	case YNL_PT_NEST:
		if (!len || len >= sizeof(*attr))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (nest %s)", policy->name);
		return -1;
	case YNL_PT_BINARY:
		if (!policy->len || len == policy->len)
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (binary %s)", policy->name);
		return -1;
	case YNL_PT_NUL_STR:
		if ((!policy->len || len <= policy->len) && !data[len - 1])
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (string %s)", policy->name);
		return -1;
	case YNL_PT_BITFIELD32:
		if (len == sizeof(struct nla_bitfield32))
			break;
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (bitfield32 %s)", policy->name);
		return -1;
	default:
		yerr(yarg->ys, YNL_ERROR_ATTR_INVALID,
		     "Invalid attribute (unknown %s)", policy->name);
		return -1;
	}

	return 0;
}

/* Generic code */

static void ynl_err_reset(struct ynl_sock *ys)
{
	ys->err.code = 0;
	ys->err.attr_offs = 0;
	ys->err.msg[0] = 0;
}

struct nlmsghdr *ynl_msg_start(struct ynl_sock *ys, __u32 id, __u16 flags)
{
	struct nlmsghdr *nlh;

	ynl_err_reset(ys);

	nlh = ys->nlh = mnl_nlmsg_put_header(ys->tx_buf);
	nlh->nlmsg_type	= id;
	nlh->nlmsg_flags = flags;
	nlh->nlmsg_seq = ++ys->seq;

	return nlh;
}

struct nlmsghdr *
ynl_gemsg_start(struct ynl_sock *ys, __u32 id, __u16 flags,
		__u8 cmd, __u8 version)
{
	struct genlmsghdr gehdr;
	struct nlmsghdr *nlh;
	void *data;

	nlh = ynl_msg_start(ys, id, flags);

	memset(&gehdr, 0, sizeof(gehdr));
	gehdr.cmd = cmd;
	gehdr.version = version;

	data = mnl_nlmsg_put_extra_header(nlh, sizeof(gehdr));
	memcpy(data, &gehdr, sizeof(gehdr));

	return nlh;
}

void ynl_msg_start_req(struct ynl_sock *ys, __u32 id)
{
	ynl_msg_start(ys, id, NLM_F_REQUEST | NLM_F_ACK);
}

void ynl_msg_start_dump(struct ynl_sock *ys, __u32 id)
{
	ynl_msg_start(ys, id, NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP);
}

struct nlmsghdr *
ynl_gemsg_start_req(struct ynl_sock *ys, __u32 id, __u8 cmd, __u8 version)
{
	return ynl_gemsg_start(ys, id, NLM_F_REQUEST | NLM_F_ACK, cmd, version);
}

struct nlmsghdr *
ynl_gemsg_start_dump(struct ynl_sock *ys, __u32 id, __u8 cmd, __u8 version)
{
	return ynl_gemsg_start(ys, id, NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP,
			       cmd, version);
}

int ynl_recv_ack(struct ynl_sock *ys, int ret)
{
	struct ynl_parse_arg yarg = { .ys = ys, };

	if (!ret) {
		yerr(ys, YNL_ERROR_EXPECT_ACK,
		     "Expecting an ACK but nothing received");
		return -1;
	}

	ret = mnl_socket_recvfrom(ys->sock, ys->rx_buf, MNL_SOCKET_BUFFER_SIZE);
	if (ret < 0) {
		perr(ys, "Socket receive failed");
		return ret;
	}
	return mnl_cb_run(ys->rx_buf, ret, ys->seq, ys->portid,
			  ynl_cb_null, &yarg);
}

int ynl_cb_null(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;

	yerr(yarg->ys, YNL_ERROR_UNEXPECT_MSG,
	     "Received a message when none were expected");

	return MNL_CB_ERROR;
}

/* Init/fini and genetlink boiler plate */
static int
ynl_get_family_info_mcast(struct ynl_sock *ys, const struct nlattr *mcasts)
{
	const struct nlattr *entry, *attr;
	unsigned int i;

	mnl_attr_for_each_nested(attr, mcasts)
		ys->n_mcast_groups++;

	if (!ys->n_mcast_groups)
		return 0;

	ys->mcast_groups = calloc(ys->n_mcast_groups,
				  sizeof(*ys->mcast_groups));
	if (!ys->mcast_groups)
		return MNL_CB_ERROR;

	i = 0;
	mnl_attr_for_each_nested(entry, mcasts) {
		mnl_attr_for_each_nested(attr, entry) {
			if (mnl_attr_get_type(attr) == CTRL_ATTR_MCAST_GRP_ID)
				ys->mcast_groups[i].id = mnl_attr_get_u32(attr);
			if (mnl_attr_get_type(attr) == CTRL_ATTR_MCAST_GRP_NAME) {
				strncpy(ys->mcast_groups[i].name,
					mnl_attr_get_str(attr),
					GENL_NAMSIZ - 1);
				ys->mcast_groups[i].name[GENL_NAMSIZ - 1] = 0;
			}
		}
	}

	return 0;
}

static int ynl_get_family_info_cb(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;
	struct ynl_sock *ys = yarg->ys;
	const struct nlattr *attr;
	bool found_id = true;

	mnl_attr_for_each(attr, nlh, sizeof(struct genlmsghdr)) {
		if (mnl_attr_get_type(attr) == CTRL_ATTR_MCAST_GROUPS)
			if (ynl_get_family_info_mcast(ys, attr))
				return MNL_CB_ERROR;

		if (mnl_attr_get_type(attr) != CTRL_ATTR_FAMILY_ID)
			continue;

		if (mnl_attr_get_payload_len(attr) != sizeof(__u16)) {
			yerr(ys, YNL_ERROR_ATTR_INVALID, "Invalid family ID");
			return MNL_CB_ERROR;
		}

		ys->family_id = mnl_attr_get_u16(attr);
		found_id = true;
	}

	if (!found_id) {
		yerr(ys, YNL_ERROR_ATTR_MISSING, "Family ID missing");
		return MNL_CB_ERROR;
	}
	return MNL_CB_OK;
}

static int ynl_sock_read_family(struct ynl_sock *ys, const char *family_name)
{
	struct ynl_parse_arg yarg = { .ys = ys, };
	struct nlmsghdr *nlh;
	int err;

	nlh = ynl_gemsg_start_req(ys, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1);
	mnl_attr_put_strz(nlh, CTRL_ATTR_FAMILY_NAME, family_name);

	err = mnl_socket_sendto(ys->sock, nlh, nlh->nlmsg_len);
	if (err < 0) {
		perr(ys, "failed to request socket family info");
		return err;
	}

	err = mnl_socket_recvfrom(ys->sock, ys->rx_buf, MNL_SOCKET_BUFFER_SIZE);
	if (err <= 0) {
		perr(ys, "failed to receive the socket family info");
		return err;
	}
	err = mnl_cb_run2(ys->rx_buf, err, ys->seq, ys->portid,
			  ynl_get_family_info_cb, &yarg,
			  ynl_cb_array, ARRAY_SIZE(ynl_cb_array));
	if (err < 0) {
		free(ys->mcast_groups);
		perr(ys, "failed to receive the socket family info - no such family?");
		return err;
	}

	err = ynl_recv_ack(ys, err);
	if (err < 0) {
		free(ys->mcast_groups);
		return err;
	}

	return 0;
}

struct ynl_sock *
ynl_sock_create(const struct ynl_family *yf, struct ynl_error *yse)
{
	struct ynl_sock *ys;
	int one = 1;

	ys = malloc(sizeof(*ys) + 2 * MNL_SOCKET_BUFFER_SIZE);
	if (!ys)
		return NULL;
	memset(ys, 0, sizeof(*ys));

	ys->family = yf;
	ys->tx_buf = &ys->raw_buf[0];
	ys->rx_buf = &ys->raw_buf[MNL_SOCKET_BUFFER_SIZE];
	ys->ntf_last_next = &ys->ntf_first;

	ys->sock = mnl_socket_open(NETLINK_GENERIC);
	if (!ys->sock) {
		__perr(yse, "failed to create a netlink socket");
		goto err_free_sock;
	}

	if (mnl_socket_setsockopt(ys->sock, NETLINK_CAP_ACK,
				  &one, sizeof(one))) {
		__perr(yse, "failed to enable netlink ACK");
		goto err_close_sock;
	}
	if (mnl_socket_setsockopt(ys->sock, NETLINK_EXT_ACK,
				  &one, sizeof(one))) {
		__perr(yse, "failed to enable netlink ext ACK");
		goto err_close_sock;
	}

	ys->seq = random();
	ys->portid = mnl_socket_get_portid(ys->sock);

	if (ynl_sock_read_family(ys, yf->name)) {
		if (yse)
			memcpy(yse, &ys->err, sizeof(*yse));
		goto err_close_sock;
	}

	return ys;

err_close_sock:
	mnl_socket_close(ys->sock);
err_free_sock:
	free(ys);
	return NULL;
}

void ynl_sock_destroy(struct ynl_sock *ys)
{
	struct ynl_ntf_base_type *ntf;

	mnl_socket_close(ys->sock);
	while ((ntf = ynl_ntf_dequeue(ys)))
		ynl_ntf_free(ntf);
	free(ys->mcast_groups);
	free(ys);
}

/* YNL multicast handling */

void ynl_ntf_free(struct ynl_ntf_base_type *ntf)
{
	ntf->free(ntf);
}

int ynl_subscribe(struct ynl_sock *ys, const char *grp_name)
{
	unsigned int i;
	int err;

	for (i = 0; i < ys->n_mcast_groups; i++)
		if (!strcmp(ys->mcast_groups[i].name, grp_name))
			break;
	if (i == ys->n_mcast_groups) {
		yerr(ys, ENOENT, "Multicast group '%s' not found", grp_name);
		return -1;
	}

	err = mnl_socket_setsockopt(ys->sock, NETLINK_ADD_MEMBERSHIP,
				    &ys->mcast_groups[i].id,
				    sizeof(ys->mcast_groups[i].id));
	if (err < 0) {
		perr(ys, "Subscribing to multicast group failed");
		return -1;
	}

	return 0;
}

int ynl_socket_get_fd(struct ynl_sock *ys)
{
	return mnl_socket_get_fd(ys->sock);
}

struct ynl_ntf_base_type *ynl_ntf_dequeue(struct ynl_sock *ys)
{
	struct ynl_ntf_base_type *ntf;

	if (!ynl_has_ntf(ys))
		return NULL;

	ntf = ys->ntf_first;
	ys->ntf_first = ntf->next;
	if (ys->ntf_last_next == &ntf->next)
		ys->ntf_last_next = &ys->ntf_first;

	return ntf;
}

static int ynl_ntf_parse(struct ynl_sock *ys, const struct nlmsghdr *nlh)
{
	struct ynl_parse_arg yarg = { .ys = ys, };
	const struct ynl_ntf_info *info;
	struct ynl_ntf_base_type *rsp;
	struct genlmsghdr *gehdr;
	int ret;

	gehdr = mnl_nlmsg_get_payload(nlh);
	if (gehdr->cmd >= ys->family->ntf_info_size)
		return MNL_CB_ERROR;
	info = &ys->family->ntf_info[gehdr->cmd];
	if (!info->cb)
		return MNL_CB_ERROR;

	rsp = calloc(1, info->alloc_sz);
	rsp->free = info->free;
	yarg.data = rsp->data;
	yarg.rsp_policy = info->policy;

	ret = info->cb(nlh, &yarg);
	if (ret <= MNL_CB_STOP)
		goto err_free;

	rsp->family = nlh->nlmsg_type;
	rsp->cmd = gehdr->cmd;

	*ys->ntf_last_next = rsp;
	ys->ntf_last_next = &rsp->next;

	return MNL_CB_OK;

err_free:
	info->free(rsp);
	return MNL_CB_ERROR;
}

static int ynl_ntf_trampoline(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_parse_arg *yarg = data;

	return ynl_ntf_parse(yarg->ys, nlh);
}

int ynl_ntf_check(struct ynl_sock *ys)
{
	struct ynl_parse_arg yarg = { .ys = ys, };
	ssize_t len;
	int err;

	do {
		/* libmnl doesn't let us pass flags to the recv to make
		 * it non-blocking so we need to poll() or peek() :|
		 */
		struct pollfd pfd = { };

		pfd.fd = mnl_socket_get_fd(ys->sock);
		pfd.events = POLLIN;
		err = poll(&pfd, 1, 1);
		if (err < 1)
			return err;

		len = mnl_socket_recvfrom(ys->sock, ys->rx_buf,
					  MNL_SOCKET_BUFFER_SIZE);
		if (len < 0)
			return len;

		err = mnl_cb_run2(ys->rx_buf, len, ys->seq, ys->portid,
				  ynl_ntf_trampoline, &yarg,
				  ynl_cb_array, NLMSG_MIN_TYPE);
		if (err < 0)
			return err;
	} while (err > 0);

	return 0;
}

/* YNL specific helpers used by the auto-generated code */

struct ynl_dump_list_type *YNL_LIST_END = (void *)(0xb4d123);

void ynl_error_unknown_notification(struct ynl_sock *ys, __u8 cmd)
{
	yerr(ys, YNL_ERROR_UNKNOWN_NTF,
	     "Unknown notification message type '%d'", cmd);
}

int ynl_error_parse(struct ynl_parse_arg *yarg, const char *msg)
{
	yerr(yarg->ys, YNL_ERROR_INV_RESP, "Error parsing response: %s", msg);
	return MNL_CB_ERROR;
}

static int
ynl_check_alien(struct ynl_sock *ys, const struct nlmsghdr *nlh, __u32 rsp_cmd)
{
	struct genlmsghdr *gehdr;

	if (mnl_nlmsg_get_payload_len(nlh) < sizeof(*gehdr)) {
		yerr(ys, YNL_ERROR_INV_RESP,
		     "Kernel responded with truncated message");
		return -1;
	}

	gehdr = mnl_nlmsg_get_payload(nlh);
	if (gehdr->cmd != rsp_cmd)
		return ynl_ntf_parse(ys, nlh);

	return 0;
}

static int ynl_req_trampoline(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_req_state *yrs = data;
	int ret;

	ret = ynl_check_alien(yrs->yarg.ys, nlh, yrs->rsp_cmd);
	if (ret)
		return ret < 0 ? MNL_CB_ERROR : MNL_CB_OK;

	return yrs->cb(nlh, &yrs->yarg);
}

int ynl_exec(struct ynl_sock *ys, struct nlmsghdr *req_nlh,
	     struct ynl_req_state *yrs)
{
	ssize_t len;
	int err;

	err = mnl_socket_sendto(ys->sock, req_nlh, req_nlh->nlmsg_len);
	if (err < 0)
		return err;

	do {
		len = mnl_socket_recvfrom(ys->sock, ys->rx_buf,
					  MNL_SOCKET_BUFFER_SIZE);
		if (len < 0)
			return len;

		err = mnl_cb_run2(ys->rx_buf, len, ys->seq, ys->portid,
				  ynl_req_trampoline, yrs,
				  ynl_cb_array, NLMSG_MIN_TYPE);
		if (err < 0)
			return err;
	} while (err > 0);

	return 0;
}

static int ynl_dump_trampoline(const struct nlmsghdr *nlh, void *data)
{
	struct ynl_dump_state *ds = data;
	struct ynl_dump_list_type *obj;
	struct ynl_parse_arg yarg = {};
	int ret;

	ret = ynl_check_alien(ds->ys, nlh, ds->rsp_cmd);
	if (ret)
		return ret < 0 ? MNL_CB_ERROR : MNL_CB_OK;

	obj = calloc(1, ds->alloc_sz);
	if (!obj)
		return MNL_CB_ERROR;

	if (!ds->first)
		ds->first = obj;
	if (ds->last)
		ds->last->next = obj;
	ds->last = obj;

	yarg.ys = ds->ys;
	yarg.rsp_policy = ds->rsp_policy;
	yarg.data = &obj->data;

	return ds->cb(nlh, &yarg);
}

static void *ynl_dump_end(struct ynl_dump_state *ds)
{
	if (!ds->first)
		return YNL_LIST_END;

	ds->last->next = YNL_LIST_END;
	return ds->first;
}

int ynl_exec_dump(struct ynl_sock *ys, struct nlmsghdr *req_nlh,
		  struct ynl_dump_state *yds)
{
	ssize_t len;
	int err;

	err = mnl_socket_sendto(ys->sock, req_nlh, req_nlh->nlmsg_len);
	if (err < 0)
		return err;

	do {
		len = mnl_socket_recvfrom(ys->sock, ys->rx_buf,
					  MNL_SOCKET_BUFFER_SIZE);
		if (len < 0)
			goto err_close_list;

		err = mnl_cb_run2(ys->rx_buf, len, ys->seq, ys->portid,
				  ynl_dump_trampoline, yds,
				  ynl_cb_array, NLMSG_MIN_TYPE);
		if (err < 0)
			goto err_close_list;
	} while (err > 0);

	yds->first = ynl_dump_end(yds);
	return 0;

err_close_list:
	yds->first = ynl_dump_end(yds);
	return -1;
}
