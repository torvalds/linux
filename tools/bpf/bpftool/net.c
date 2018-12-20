// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2018 Facebook

#define _GNU_SOURCE
#include <errno.h>
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

struct ip_devname_ifindex {
	char	devname[64];
	int	ifindex;
};

struct bpf_netdev_t {
	struct ip_devname_ifindex *devices;
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

struct bpf_filter_t {
	const char	*kind;
	const char	*devname;
	int		ifindex;
};

static int dump_link_nlmsg(void *cookie, void *msg, struct nlattr **tb)
{
	struct bpf_netdev_t *netinfo = cookie;
	struct ifinfomsg *ifinfo = msg;

	if (netinfo->filter_idx > 0 && netinfo->filter_idx != ifinfo->ifi_index)
		return 0;

	if (netinfo->used_len == netinfo->array_len) {
		netinfo->devices = realloc(netinfo->devices,
			(netinfo->array_len + 16) *
			sizeof(struct ip_devname_ifindex));
		if (!netinfo->devices)
			return -ENOMEM;

		netinfo->array_len += 16;
	}
	netinfo->devices[netinfo->used_len].ifindex = ifinfo->ifi_index;
	snprintf(netinfo->devices[netinfo->used_len].devname,
		 sizeof(netinfo->devices[netinfo->used_len].devname),
		 "%s",
		 tb[IFLA_IFNAME]
			 ? libbpf_nla_getattr_str(tb[IFLA_IFNAME])
			 : "");
	netinfo->used_len++;

	return do_xdp_dump(ifinfo, tb);
}

static int dump_class_qdisc_nlmsg(void *cookie, void *msg, struct nlattr **tb)
{
	struct bpf_tcinfo_t *tcinfo = cookie;
	struct tcmsg *info = msg;

	if (tcinfo->is_qdisc) {
		/* skip clsact qdisc */
		if (tb[TCA_KIND] &&
		    strcmp(libbpf_nla_data(tb[TCA_KIND]), "clsact") == 0)
			return 0;
		if (info->tcm_handle == 0)
			return 0;
	}

	if (tcinfo->used_len == tcinfo->array_len) {
		tcinfo->handle_array = realloc(tcinfo->handle_array,
			(tcinfo->array_len + 16) * sizeof(struct tc_kind_handle));
		if (!tcinfo->handle_array)
			return -ENOMEM;

		tcinfo->array_len += 16;
	}
	tcinfo->handle_array[tcinfo->used_len].handle = info->tcm_handle;
	snprintf(tcinfo->handle_array[tcinfo->used_len].kind,
		 sizeof(tcinfo->handle_array[tcinfo->used_len].kind),
		 "%s",
		 tb[TCA_KIND]
			 ? libbpf_nla_getattr_str(tb[TCA_KIND])
			 : "unknown");
	tcinfo->used_len++;

	return 0;
}

static int dump_filter_nlmsg(void *cookie, void *msg, struct nlattr **tb)
{
	const struct bpf_filter_t *filter_info = cookie;

	return do_filter_dump((struct tcmsg *)msg, tb, filter_info->kind,
			      filter_info->devname, filter_info->ifindex);
}

static int show_dev_tc_bpf(int sock, unsigned int nl_pid,
			   struct ip_devname_ifindex *dev)
{
	struct bpf_filter_t filter_info;
	struct bpf_tcinfo_t tcinfo;
	int i, handle, ret = 0;

	tcinfo.handle_array = NULL;
	tcinfo.used_len = 0;
	tcinfo.array_len = 0;

	tcinfo.is_qdisc = false;
	ret = libbpf_nl_get_class(sock, nl_pid, dev->ifindex,
				  dump_class_qdisc_nlmsg, &tcinfo);
	if (ret)
		goto out;

	tcinfo.is_qdisc = true;
	ret = libbpf_nl_get_qdisc(sock, nl_pid, dev->ifindex,
				  dump_class_qdisc_nlmsg, &tcinfo);
	if (ret)
		goto out;

	filter_info.devname = dev->devname;
	filter_info.ifindex = dev->ifindex;
	for (i = 0; i < tcinfo.used_len; i++) {
		filter_info.kind = tcinfo.handle_array[i].kind;
		ret = libbpf_nl_get_filter(sock, nl_pid, dev->ifindex,
					   tcinfo.handle_array[i].handle,
					   dump_filter_nlmsg, &filter_info);
		if (ret)
			goto out;
	}

	/* root, ingress and egress handle */
	handle = TC_H_ROOT;
	filter_info.kind = "root";
	ret = libbpf_nl_get_filter(sock, nl_pid, dev->ifindex, handle,
				   dump_filter_nlmsg, &filter_info);
	if (ret)
		goto out;

	handle = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	filter_info.kind = "clsact/ingress";
	ret = libbpf_nl_get_filter(sock, nl_pid, dev->ifindex, handle,
				   dump_filter_nlmsg, &filter_info);
	if (ret)
		goto out;

	handle = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_EGRESS);
	filter_info.kind = "clsact/egress";
	ret = libbpf_nl_get_filter(sock, nl_pid, dev->ifindex, handle,
				   dump_filter_nlmsg, &filter_info);
	if (ret)
		goto out;

out:
	free(tcinfo.handle_array);
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

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0) {
		fprintf(stderr, "failed to open netlink sock\n");
		return -1;
	}

	dev_array.devices = NULL;
	dev_array.used_len = 0;
	dev_array.array_len = 0;
	dev_array.filter_idx = filter_idx;

	if (json_output)
		jsonw_start_array(json_wtr);
	NET_START_OBJECT;
	NET_START_ARRAY("xdp", "%s:\n");
	ret = libbpf_nl_get_link(sock, nl_pid, dump_link_nlmsg, &dev_array);
	NET_END_ARRAY("\n");

	if (!ret) {
		NET_START_ARRAY("tc", "%s:\n");
		for (i = 0; i < dev_array.used_len; i++) {
			ret = show_dev_tc_bpf(sock, nl_pid,
					      &dev_array.devices[i]);
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
	free(dev_array.devices);
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
		"       %s %s help\n"
		"Note: Only xdp and tc attachments are supported now.\n"
		"      For progs attached to cgroups, use \"bpftool cgroup\"\n"
		"      to dump program attachments. For program types\n"
		"      sk_{filter,skb,msg,reuseport} and lwt/seg6, please\n"
		"      consult iproute2.\n",
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
