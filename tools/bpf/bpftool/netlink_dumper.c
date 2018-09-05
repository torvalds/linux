// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2018 Facebook

#include <stdlib.h>
#include <string.h>
#include <libbpf.h>
#include <linux/rtnetlink.h>
#include <linux/tc_act/tc_bpf.h>

#include <nlattr.h>
#include "main.h"
#include "netlink_dumper.h"

static void xdp_dump_prog_id(struct nlattr **tb, int attr,
			     const char *type)
{
	if (!tb[attr])
		return;

	NET_DUMP_UINT(type, nla_getattr_u32(tb[attr]))
}

static int do_xdp_dump_one(struct nlattr *attr, unsigned int ifindex,
			   const char *name)
{
	struct nlattr *tb[IFLA_XDP_MAX + 1];
	unsigned char mode;

	if (nla_parse_nested(tb, IFLA_XDP_MAX, attr, NULL) < 0)
		return -1;

	if (!tb[IFLA_XDP_ATTACHED])
		return 0;

	mode = nla_getattr_u8(tb[IFLA_XDP_ATTACHED]);
	if (mode == XDP_ATTACHED_NONE)
		return 0;

	NET_START_OBJECT;
	NET_DUMP_UINT("ifindex", ifindex);

	if (name)
		NET_DUMP_STR("devname", name);

	if (tb[IFLA_XDP_PROG_ID])
		NET_DUMP_UINT("prog_id", nla_getattr_u32(tb[IFLA_XDP_PROG_ID]));

	if (mode == XDP_ATTACHED_MULTI) {
		xdp_dump_prog_id(tb, IFLA_XDP_SKB_PROG_ID, "generic_prog_id");
		xdp_dump_prog_id(tb, IFLA_XDP_DRV_PROG_ID, "drv_prog_id");
		xdp_dump_prog_id(tb, IFLA_XDP_HW_PROG_ID, "offload_prog_id");
	}

	NET_END_OBJECT_FINAL;
	return 0;
}

int do_xdp_dump(struct ifinfomsg *ifinfo, struct nlattr **tb)
{
	if (!tb[IFLA_XDP])
		return 0;

	return do_xdp_dump_one(tb[IFLA_XDP], ifinfo->ifi_index,
			       nla_getattr_str(tb[IFLA_IFNAME]));
}

static char *hexstring_n2a(const unsigned char *str, int len,
			   char *buf, int blen)
{
	char *ptr = buf;
	int i;

	for (i = 0; i < len; i++) {
		if (blen < 3)
			break;
		sprintf(ptr, "%02x", str[i]);
		ptr += 2;
		blen -= 2;
	}
	return buf;
}

static int do_bpf_dump_one_act(struct nlattr *attr)
{
	struct nlattr *tb[TCA_ACT_BPF_MAX + 1];
	char buf[256];

	if (nla_parse_nested(tb, TCA_ACT_BPF_MAX, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	if (!tb[TCA_ACT_BPF_PARMS])
		return -LIBBPF_ERRNO__NLPARSE;

	NET_START_OBJECT_NESTED2;
	if (tb[TCA_ACT_BPF_NAME])
		NET_DUMP_STR("name", nla_getattr_str(tb[TCA_ACT_BPF_NAME]));
	if (tb[TCA_ACT_BPF_ID])
		NET_DUMP_UINT("bpf_id", nla_getattr_u32(tb[TCA_ACT_BPF_ID]));
	if (tb[TCA_ACT_BPF_TAG])
		NET_DUMP_STR("tag", hexstring_n2a(nla_data(tb[TCA_ACT_BPF_TAG]),
						  nla_len(tb[TCA_ACT_BPF_TAG]),
						  buf, sizeof(buf)));
	NET_END_OBJECT_NESTED;
	return 0;
}

static int do_dump_one_act(struct nlattr *attr)
{
	struct nlattr *tb[TCA_ACT_MAX + 1];

	if (!attr)
		return 0;

	if (nla_parse_nested(tb, TCA_ACT_MAX, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	if (tb[TCA_ACT_KIND] && strcmp(nla_data(tb[TCA_ACT_KIND]), "bpf") == 0)
		return do_bpf_dump_one_act(tb[TCA_ACT_OPTIONS]);

	return 0;
}

static int do_bpf_act_dump(struct nlattr *attr)
{
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	int act, ret;

	if (nla_parse_nested(tb, TCA_ACT_MAX_PRIO, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	NET_START_ARRAY("act", "");
	for (act = 0; act <= TCA_ACT_MAX_PRIO; act++) {
		ret = do_dump_one_act(tb[act]);
		if (ret)
			break;
	}
	NET_END_ARRAY(" ");

	return ret;
}

static int do_bpf_filter_dump(struct nlattr *attr)
{
	struct nlattr *tb[TCA_BPF_MAX + 1];
	char buf[256];
	int ret;

	if (nla_parse_nested(tb, TCA_BPF_MAX, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	if (tb[TCA_BPF_NAME])
		NET_DUMP_STR("name", nla_getattr_str(tb[TCA_BPF_NAME]));
	if (tb[TCA_BPF_ID])
		NET_DUMP_UINT("prog_id", nla_getattr_u32(tb[TCA_BPF_ID]));
	if (tb[TCA_BPF_TAG])
		NET_DUMP_STR("tag", hexstring_n2a(nla_data(tb[TCA_BPF_TAG]),
						  nla_len(tb[TCA_BPF_TAG]),
						  buf, sizeof(buf)));
	if (tb[TCA_BPF_ACT]) {
		ret = do_bpf_act_dump(tb[TCA_BPF_ACT]);
		if (ret)
			return ret;
	}

	return 0;
}

int do_filter_dump(struct tcmsg *info, struct nlattr **tb, const char *kind)
{
	int ret = 0;

	if (tb[TCA_OPTIONS] && strcmp(nla_data(tb[TCA_KIND]), "bpf") == 0) {
		NET_START_OBJECT;
		NET_DUMP_UINT("ifindex", info->tcm_ifindex);
		NET_DUMP_STR("kind", kind);
		ret = do_bpf_filter_dump(tb[TCA_OPTIONS]);
		NET_END_OBJECT_FINAL;
	}

	return ret;
}
