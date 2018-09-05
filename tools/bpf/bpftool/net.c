// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2018 Facebook

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libbpf.h>
#include <net/if.h>
#include <linux/if.h>
#include <linux/rtnetlink.h>
#include <linux/tc_act/tc_bpf.h>
#include <sys/socket.h>

#include <bpf.h>
#include <nlattr.h>
#include "main.h"
#include "netlink_dumper.h"

struct bpf_netdev_t {
	int	*ifindex_array;
	int	used_len;
	int	array_len;
	int	filter_idx;
};

struct tc_kind_handle {
	char	kind[64];
	int	handle;
};

struct bpf_tcinfo_t {
	struct tc_kind_handle	*handle_array;
	int			used_len;
	int			array_len;
	bool			is_qdisc;
};

static int dump_link_nlmsg(void *cookie, void *msg, struct nlattr **tb)
{
	struct bpf_netdev_t *netinfo = cookie;
	struct ifinfomsg *ifinfo = msg;

	if (netinfo->filter_idx > 0 && netinfo->filter_idx != ifinfo->ifi_index)
		return 0;

	if (netinfo->used_len == netinfo->array_len) {
		netinfo->ifindex_array = realloc(netinfo->ifindex_array,
			(netinfo->array_len + 16) * sizeof(int));
		netinfo->array_len += 16;
	}
	netinfo->ifindex_array[netinfo->used_len++] = ifinfo->ifi_index;

	return do_xdp_dump(ifinfo, tb);
}

static int dump_class_qdisc_nlmsg(void *cookie, void *msg, struct nlattr **tb)
{
	struct bpf_tcinfo_t *tcinfo = cookie;
	struct tcmsg *info = msg;

	if (tcinfo->is_qdisc) {
		/* skip clsact qdisc */
		if (tb[TCA_KIND] &&
		    strcmp(nla_data(tb[TCA_KIND]), "clsact") == 0)
			return 0;
		if (info->tcm_handle == 0)
			return 0;
	}

	if (tcinfo->used_len == tcinfo->array_len) {
		tcinfo->handle_array = realloc(tcinfo->handle_array,
			(tcinfo->array_len + 16) * sizeof(struct tc_kind_handle));
		tcinfo->array_len += 16;
	}
	tcinfo->handle_array[tcinfo->used_len].handle = info->tcm_handle;
	snprintf(tcinfo->handle_array[tcinfo->used_len].kind,
		 sizeof(tcinfo->handle_array[tcinfo->used_len].kind),
		 "%s_%s",
		 tcinfo->is_qdisc ? "qdisc" : "class",
		 tb[TCA_KIND] ? nla_getattr_str(tb[TCA_KIND]) : "unknown");
	tcinfo->used_len++;

	return 0;
}

static int dump_filter_nlmsg(void *cookie, void *msg, struct nlattr **tb)
{
	const char *kind = cookie;

	return do_filter_dump((struct tcmsg *)msg, tb, kind);
}

static int show_dev_tc_bpf(int sock, unsigned int nl_pid, int ifindex)
{
	struct bpf_tcinfo_t tcinfo;
	int i, handle, ret;

	tcinfo.handle_array = NULL;
	tcinfo.used_len = 0;
	tcinfo.array_len = 0;

	tcinfo.is_qdisc = false;
	ret = nl_get_class(sock, nl_pid, ifindex, dump_class_qdisc_nlmsg,
			   &tcinfo);
	if (ret)
		return ret;

	tcinfo.is_qdisc = true;
	ret = nl_get_qdisc(sock, nl_pid, ifindex, dump_class_qdisc_nlmsg,
			   &tcinfo);
	if (ret)
		return ret;

	for (i = 0; i < tcinfo.used_len; i++) {
		ret = nl_get_filter(sock, nl_pid, ifindex,
				    tcinfo.handle_array[i].handle,
				    dump_filter_nlmsg,
				    tcinfo.handle_array[i].kind);
		if (ret)
			return ret;
	}

	/* root, ingress and egress handle */
	handle = TC_H_ROOT;
	ret = nl_get_filter(sock, nl_pid, ifindex, handle, dump_filter_nlmsg,
			    "root");
	if (ret)
		return ret;

	handle = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	ret = nl_get_filter(sock, nl_pid, ifindex, handle, dump_filter_nlmsg,
			    "qdisc_clsact_ingress");
	if (ret)
		return ret;

	handle = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_EGRESS);
	ret = nl_get_filter(sock, nl_pid, ifindex, handle, dump_filter_nlmsg,
			    "qdisc_clsact_egress");
	if (ret)
		return ret;

	return 0;
}

static int do_show(int argc, char **argv)
{
	int i, sock, ret, filter_idx = -1;
	struct bpf_netdev_t dev_array;
	unsigned int nl_pid;
	char err_buf[256];

	if (argc == 2) {
		if (strcmp(argv[0], "dev") != 0)
			usage();
		filter_idx = if_nametoindex(argv[1]);
		if (filter_idx == 0) {
			fprintf(stderr, "invalid dev name %s\n", argv[1]);
			return -1;
		}
	} else if (argc != 0) {
		usage();
	}

	sock = bpf_netlink_open(&nl_pid);
	if (sock < 0) {
		fprintf(stderr, "failed to open netlink sock\n");
		return -1;
	}

	dev_array.ifindex_array = NULL;
	dev_array.used_len = 0;
	dev_array.array_len = 0;
	dev_array.filter_idx = filter_idx;

	if (json_output)
		jsonw_start_array(json_wtr);
	NET_START_OBJECT;
	NET_START_ARRAY("xdp", "\n");
	ret = nl_get_link(sock, nl_pid, dump_link_nlmsg, &dev_array);
	NET_END_ARRAY("\n");

	if (!ret) {
		NET_START_ARRAY("tc_filters", "\n");
		for (i = 0; i < dev_array.used_len; i++) {
			ret = show_dev_tc_bpf(sock, nl_pid,
					      dev_array.ifindex_array[i]);
			if (ret)
				break;
		}
		NET_END_ARRAY("\n");
	}
	NET_END_OBJECT;
	if (json_output)
		jsonw_end_array(json_wtr);

	if (ret) {
		if (json_output)
			jsonw_null(json_wtr);
		libbpf_strerror(ret, err_buf, sizeof(err_buf));
		fprintf(stderr, "Error: %s\n", err_buf);
	}
	free(dev_array.ifindex_array);
	close(sock);
	return ret;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s { show | list } [dev <devname>]\n"
		"       %s %s help\n",
		bin_name, argv[-2], bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "help",	do_help },
	{ 0 }
};

int do_net(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
