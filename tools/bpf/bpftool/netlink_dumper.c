// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
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
			     const char *mode,
			     bool new_json_object)
{
	if (!tb[attr])
		return;

	if (new_json_object)
		NET_START_OBJECT
	NET_DUMP_STR("mode", " %s", mode);
	NET_DUMP_UINT("id", " id %u", libbpf_nla_getattr_u32(tb[attr]))
	if (new_json_object)
		NET_END_OBJECT
}

static int do_xdp_dump_one(struct nlattr *attr, unsigned int ifindex,
			   const char *name)
{
	struct nlattr *tb[IFLA_XDP_MAX + 1];
	unsigned char mode;

	if (libbpf_nla_parse_nested(tb, IFLA_XDP_MAX, attr, NULL) < 0)
		return -1;

	if (!tb[IFLA_XDP_ATTACHED])
		return 0;

	mode = libbpf_nla_getattr_u8(tb[IFLA_XDP_ATTACHED]);
	if (mode == XDP_ATTACHED_NONE)
		return 0;

	NET_START_OBJECT;
	if (name)
		NET_DUMP_STR("devname", "%s", name);
	NET_DUMP_UINT("ifindex", "(%d)", ifindex);

	if (mode == XDP_ATTACHED_MULTI) {
		if (json_output) {
			jsonw_name(json_wtr, "multi_attachments");
			jsonw_start_array(json_wtr);
		}
		xdp_dump_prog_id(tb, IFLA_XDP_SKB_PROG_ID, "generic", true);
		xdp_dump_prog_id(tb, IFLA_XDP_DRV_PROG_ID, "driver", true);
		xdp_dump_prog_id(tb, IFLA_XDP_HW_PROG_ID, "offload", true);
		if (json_output)
			jsonw_end_array(json_wtr);
	} else if (mode == XDP_ATTACHED_DRV) {
		xdp_dump_prog_id(tb, IFLA_XDP_PROG_ID, "driver", false);
	} else if (mode == XDP_ATTACHED_SKB) {
		xdp_dump_prog_id(tb, IFLA_XDP_PROG_ID, "generic", false);
	} else if (mode == XDP_ATTACHED_HW) {
		xdp_dump_prog_id(tb, IFLA_XDP_PROG_ID, "offload", false);
	}

	NET_END_OBJECT_FINAL;
	return 0;
}

int do_xdp_dump(struct ifinfomsg *ifinfo, struct nlattr **tb)
{
	if (!tb[IFLA_XDP])
		return 0;

	return do_xdp_dump_one(tb[IFLA_XDP], ifinfo->ifi_index,
			       libbpf_nla_getattr_str(tb[IFLA_IFNAME]));
}

static int do_bpf_dump_one_act(struct nlattr *attr)
{
	struct nlattr *tb[TCA_ACT_BPF_MAX + 1];

	if (libbpf_nla_parse_nested(tb, TCA_ACT_BPF_MAX, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	if (!tb[TCA_ACT_BPF_PARMS])
		return -LIBBPF_ERRNO__NLPARSE;

	NET_START_OBJECT_NESTED2;
	if (tb[TCA_ACT_BPF_NAME])
		NET_DUMP_STR("name", "%s",
			     libbpf_nla_getattr_str(tb[TCA_ACT_BPF_NAME]));
	if (tb[TCA_ACT_BPF_ID])
		NET_DUMP_UINT("id", " id %u",
			      libbpf_nla_getattr_u32(tb[TCA_ACT_BPF_ID]));
	NET_END_OBJECT_NESTED;
	return 0;
}

static int do_dump_one_act(struct nlattr *attr)
{
	struct nlattr *tb[TCA_ACT_MAX + 1];

	if (!attr)
		return 0;

	if (libbpf_nla_parse_nested(tb, TCA_ACT_MAX, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	if (tb[TCA_ACT_KIND] &&
	    strcmp(libbpf_nla_data(tb[TCA_ACT_KIND]), "bpf") == 0)
		return do_bpf_dump_one_act(tb[TCA_ACT_OPTIONS]);

	return 0;
}

static int do_bpf_act_dump(struct nlattr *attr)
{
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	int act, ret;

	if (libbpf_nla_parse_nested(tb, TCA_ACT_MAX_PRIO, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	NET_START_ARRAY("act", " %s [");
	for (act = 0; act <= TCA_ACT_MAX_PRIO; act++) {
		ret = do_dump_one_act(tb[act]);
		if (ret)
			break;
	}
	NET_END_ARRAY("] ");

	return ret;
}

static int do_bpf_filter_dump(struct nlattr *attr)
{
	struct nlattr *tb[TCA_BPF_MAX + 1];
	int ret;

	if (libbpf_nla_parse_nested(tb, TCA_BPF_MAX, attr, NULL) < 0)
		return -LIBBPF_ERRNO__NLPARSE;

	if (tb[TCA_BPF_NAME])
		NET_DUMP_STR("name", " %s",
			     libbpf_nla_getattr_str(tb[TCA_BPF_NAME]));
	if (tb[TCA_BPF_ID])
		NET_DUMP_UINT("id", " id %u",
			      libbpf_nla_getattr_u32(tb[TCA_BPF_ID]));
	if (tb[TCA_BPF_ACT]) {
		ret = do_bpf_act_dump(tb[TCA_BPF_ACT]);
		if (ret)
			return ret;
	}

	return 0;
}

int do_filter_dump(struct tcmsg *info, struct nlattr **tb, const char *kind,
		   const char *devname, int ifindex)
{
	int ret = 0;

	if (tb[TCA_OPTIONS] &&
	    strcmp(libbpf_nla_data(tb[TCA_KIND]), "bpf") == 0) {
		NET_START_OBJECT;
		if (devname[0] != '\0')
			NET_DUMP_STR("devname", "%s", devname);
		NET_DUMP_UINT("ifindex", "(%u)", ifindex);
		NET_DUMP_STR("kind", " %s", kind);
		ret = do_bpf_filter_dump(tb[TCA_OPTIONS]);
		NET_END_OBJECT_FINAL;
	}

	return ret;
}
