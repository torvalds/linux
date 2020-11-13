// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (C) 2018 Facebook

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <net/if.h>
#include <linux/if.h>
#include <linux/rtnetlink.h>
#include <linux/socket.h>
#include <linux/tc_act/tc_bpf.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "bpf/nlattr.h"
#include "main.h"
#include "netlink_dumper.h"

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

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

struct bpf_attach_info {
	__u32 flow_dissector_id;
};

enum net_attach_type {
	NET_ATTACH_TYPE_XDP,
	NET_ATTACH_TYPE_XDP_GENERIC,
	NET_ATTACH_TYPE_XDP_DRIVER,
	NET_ATTACH_TYPE_XDP_OFFLOAD,
};

static const char * const attach_type_strings[] = {
	[NET_ATTACH_TYPE_XDP]		= "xdp",
	[NET_ATTACH_TYPE_XDP_GENERIC]	= "xdpgeneric",
	[NET_ATTACH_TYPE_XDP_DRIVER]	= "xdpdrv",
	[NET_ATTACH_TYPE_XDP_OFFLOAD]	= "xdpoffload",
};

const size_t net_attach_type_size = ARRAY_SIZE(attach_type_strings);

static enum net_attach_type parse_attach_type(const char *str)
{
	enum net_attach_type type;

	for (type = 0; type < net_attach_type_size; type++) {
		if (attach_type_strings[type] &&
		    is_prefix(str, attach_type_strings[type]))
			return type;
	}

	return net_attach_type_size;
}

typedef int (*dump_nlmsg_t)(void *cookie, void *msg, struct nlattr **tb);

typedef int (*__dump_nlmsg_t)(struct nlmsghdr *nlmsg, dump_nlmsg_t, void *cookie);

static int netlink_open(__u32 *nl_pid)
{
	struct sockaddr_nl sa;
	socklen_t addrlen;
	int one = 1, ret;
	int sock;

	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;

	sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (sock < 0)
		return -errno;

	if (setsockopt(sock, SOL_NETLINK, NETLINK_EXT_ACK,
		       &one, sizeof(one)) < 0) {
		p_err("Netlink error reporting not supported");
	}

	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		ret = -errno;
		goto cleanup;
	}

	addrlen = sizeof(sa);
	if (getsockname(sock, (struct sockaddr *)&sa, &addrlen) < 0) {
		ret = -errno;
		goto cleanup;
	}

	if (addrlen != sizeof(sa)) {
		ret = -LIBBPF_ERRNO__INTERNAL;
		goto cleanup;
	}

	*nl_pid = sa.nl_pid;
	return sock;

cleanup:
	close(sock);
	return ret;
}

static int netlink_recv(int sock, __u32 nl_pid, __u32 seq,
			    __dump_nlmsg_t _fn, dump_nlmsg_t fn,
			    void *cookie)
{
	bool multipart = true;
	struct nlmsgerr *err;
	struct nlmsghdr *nh;
	char buf[4096];
	int len, ret;

	while (multipart) {
		multipart = false;
		len = recv(sock, buf, sizeof(buf), 0);
		if (len < 0) {
			ret = -errno;
			goto done;
		}

		if (len == 0)
			break;

		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len);
		     nh = NLMSG_NEXT(nh, len)) {
			if (nh->nlmsg_pid != nl_pid) {
				ret = -LIBBPF_ERRNO__WRNGPID;
				goto done;
			}
			if (nh->nlmsg_seq != seq) {
				ret = -LIBBPF_ERRNO__INVSEQ;
				goto done;
			}
			if (nh->nlmsg_flags & NLM_F_MULTI)
				multipart = true;
			switch (nh->nlmsg_type) {
			case NLMSG_ERROR:
				err = (struct nlmsgerr *)NLMSG_DATA(nh);
				if (!err->error)
					continue;
				ret = err->error;
				libbpf_nla_dump_errormsg(nh);
				goto done;
			case NLMSG_DONE:
				return 0;
			default:
				break;
			}
			if (_fn) {
				ret = _fn(nh, fn, cookie);
				if (ret)
					return ret;
			}
		}
	}
	ret = 0;
done:
	return ret;
}

static int __dump_class_nlmsg(struct nlmsghdr *nlh,
			      dump_nlmsg_t dump_class_nlmsg,
			      void *cookie)
{
	struct nlattr *tb[TCA_MAX + 1], *attr;
	struct tcmsg *t = NLMSG_DATA(nlh);
	int len;

	len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*t));
	attr = (struct nlattr *) ((void *) t + NLMSG_ALIGN(sizeof(*t)));
	if (libbpf_nla_parse(tb, TCA_MAX, attr, len, NULL) != 0)
		return -LIBBPF_ERRNO__NLPARSE;

	return dump_class_nlmsg(cookie, t, tb);
}

static int netlink_get_class(int sock, unsigned int nl_pid, int ifindex,
			     dump_nlmsg_t dump_class_nlmsg, void *cookie)
{
	struct {
		struct nlmsghdr nlh;
		struct tcmsg t;
	} req = {
		.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
		.nlh.nlmsg_type = RTM_GETTCLASS,
		.nlh.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST,
		.t.tcm_family = AF_UNSPEC,
		.t.tcm_ifindex = ifindex,
	};
	int seq = time(NULL);

	req.nlh.nlmsg_seq = seq;
	if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0)
		return -errno;

	return netlink_recv(sock, nl_pid, seq, __dump_class_nlmsg,
			    dump_class_nlmsg, cookie);
}

static int __dump_qdisc_nlmsg(struct nlmsghdr *nlh,
			      dump_nlmsg_t dump_qdisc_nlmsg,
			      void *cookie)
{
	struct nlattr *tb[TCA_MAX + 1], *attr;
	struct tcmsg *t = NLMSG_DATA(nlh);
	int len;

	len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*t));
	attr = (struct nlattr *) ((void *) t + NLMSG_ALIGN(sizeof(*t)));
	if (libbpf_nla_parse(tb, TCA_MAX, attr, len, NULL) != 0)
		return -LIBBPF_ERRNO__NLPARSE;

	return dump_qdisc_nlmsg(cookie, t, tb);
}

static int netlink_get_qdisc(int sock, unsigned int nl_pid, int ifindex,
			     dump_nlmsg_t dump_qdisc_nlmsg, void *cookie)
{
	struct {
		struct nlmsghdr nlh;
		struct tcmsg t;
	} req = {
		.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
		.nlh.nlmsg_type = RTM_GETQDISC,
		.nlh.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST,
		.t.tcm_family = AF_UNSPEC,
		.t.tcm_ifindex = ifindex,
	};
	int seq = time(NULL);

	req.nlh.nlmsg_seq = seq;
	if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0)
		return -errno;

	return netlink_recv(sock, nl_pid, seq, __dump_qdisc_nlmsg,
			    dump_qdisc_nlmsg, cookie);
}

static int __dump_filter_nlmsg(struct nlmsghdr *nlh,
			       dump_nlmsg_t dump_filter_nlmsg,
			       void *cookie)
{
	struct nlattr *tb[TCA_MAX + 1], *attr;
	struct tcmsg *t = NLMSG_DATA(nlh);
	int len;

	len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*t));
	attr = (struct nlattr *) ((void *) t + NLMSG_ALIGN(sizeof(*t)));
	if (libbpf_nla_parse(tb, TCA_MAX, attr, len, NULL) != 0)
		return -LIBBPF_ERRNO__NLPARSE;

	return dump_filter_nlmsg(cookie, t, tb);
}

static int netlink_get_filter(int sock, unsigned int nl_pid, int ifindex, int handle,
			      dump_nlmsg_t dump_filter_nlmsg, void *cookie)
{
	struct {
		struct nlmsghdr nlh;
		struct tcmsg t;
	} req = {
		.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
		.nlh.nlmsg_type = RTM_GETTFILTER,
		.nlh.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST,
		.t.tcm_family = AF_UNSPEC,
		.t.tcm_ifindex = ifindex,
		.t.tcm_parent = handle,
	};
	int seq = time(NULL);

	req.nlh.nlmsg_seq = seq;
	if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0)
		return -errno;

	return netlink_recv(sock, nl_pid, seq, __dump_filter_nlmsg,
			    dump_filter_nlmsg, cookie);
}

static int __dump_link_nlmsg(struct nlmsghdr *nlh,
			     dump_nlmsg_t dump_link_nlmsg, void *cookie)
{
	struct nlattr *tb[IFLA_MAX + 1], *attr;
	struct ifinfomsg *ifi = NLMSG_DATA(nlh);
	int len;

	len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));
	attr = (struct nlattr *) ((void *) ifi + NLMSG_ALIGN(sizeof(*ifi)));
	if (libbpf_nla_parse(tb, IFLA_MAX, attr, len, NULL) != 0)
		return -LIBBPF_ERRNO__NLPARSE;

	return dump_link_nlmsg(cookie, ifi, tb);
}

static int netlink_get_link(int sock, unsigned int nl_pid,
			    dump_nlmsg_t dump_link_nlmsg, void *cookie)
{
	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
	} req = {
		.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
		.nlh.nlmsg_type = RTM_GETLINK,
		.nlh.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST,
		.ifm.ifi_family = AF_PACKET,
	};
	int seq = time(NULL);

	req.nlh.nlmsg_seq = seq;
	if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0)
		return -errno;

	return netlink_recv(sock, nl_pid, seq, __dump_link_nlmsg,
			    dump_link_nlmsg, cookie);
}

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
	ret = netlink_get_class(sock, nl_pid, dev->ifindex,
				dump_class_qdisc_nlmsg, &tcinfo);
	if (ret)
		goto out;

	tcinfo.is_qdisc = true;
	ret = netlink_get_qdisc(sock, nl_pid, dev->ifindex,
				dump_class_qdisc_nlmsg, &tcinfo);
	if (ret)
		goto out;

	filter_info.devname = dev->devname;
	filter_info.ifindex = dev->ifindex;
	for (i = 0; i < tcinfo.used_len; i++) {
		filter_info.kind = tcinfo.handle_array[i].kind;
		ret = netlink_get_filter(sock, nl_pid, dev->ifindex,
					 tcinfo.handle_array[i].handle,
					 dump_filter_nlmsg, &filter_info);
		if (ret)
			goto out;
	}

	/* root, ingress and egress handle */
	handle = TC_H_ROOT;
	filter_info.kind = "root";
	ret = netlink_get_filter(sock, nl_pid, dev->ifindex, handle,
				 dump_filter_nlmsg, &filter_info);
	if (ret)
		goto out;

	handle = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	filter_info.kind = "clsact/ingress";
	ret = netlink_get_filter(sock, nl_pid, dev->ifindex, handle,
				 dump_filter_nlmsg, &filter_info);
	if (ret)
		goto out;

	handle = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_EGRESS);
	filter_info.kind = "clsact/egress";
	ret = netlink_get_filter(sock, nl_pid, dev->ifindex, handle,
				 dump_filter_nlmsg, &filter_info);
	if (ret)
		goto out;

out:
	free(tcinfo.handle_array);
	return 0;
}

static int query_flow_dissector(struct bpf_attach_info *attach_info)
{
	__u32 attach_flags;
	__u32 prog_ids[1];
	__u32 prog_cnt;
	int err;
	int fd;

	fd = open("/proc/self/ns/net", O_RDONLY);
	if (fd < 0) {
		p_err("can't open /proc/self/ns/net: %s",
		      strerror(errno));
		return -1;
	}
	prog_cnt = ARRAY_SIZE(prog_ids);
	err = bpf_prog_query(fd, BPF_FLOW_DISSECTOR, 0,
			     &attach_flags, prog_ids, &prog_cnt);
	close(fd);
	if (err) {
		if (errno == EINVAL) {
			/* Older kernel's don't support querying
			 * flow dissector programs.
			 */
			errno = 0;
			return 0;
		}
		p_err("can't query prog: %s", strerror(errno));
		return -1;
	}

	if (prog_cnt == 1)
		attach_info->flow_dissector_id = prog_ids[0];

	return 0;
}

static int net_parse_dev(int *argc, char ***argv)
{
	int ifindex;

	if (is_prefix(**argv, "dev")) {
		NEXT_ARGP();

		ifindex = if_nametoindex(**argv);
		if (!ifindex)
			p_err("invalid devname %s", **argv);

		NEXT_ARGP();
	} else {
		p_err("expected 'dev', got: '%s'?", **argv);
		return -1;
	}

	return ifindex;
}

static int do_attach_detach_xdp(int progfd, enum net_attach_type attach_type,
				int ifindex, bool overwrite)
{
	__u32 flags = 0;

	if (!overwrite)
		flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
	if (attach_type == NET_ATTACH_TYPE_XDP_GENERIC)
		flags |= XDP_FLAGS_SKB_MODE;
	if (attach_type == NET_ATTACH_TYPE_XDP_DRIVER)
		flags |= XDP_FLAGS_DRV_MODE;
	if (attach_type == NET_ATTACH_TYPE_XDP_OFFLOAD)
		flags |= XDP_FLAGS_HW_MODE;

	return bpf_set_link_xdp_fd(ifindex, progfd, flags);
}

static int do_attach(int argc, char **argv)
{
	enum net_attach_type attach_type;
	int progfd, ifindex, err = 0;
	bool overwrite = false;

	/* parse attach args */
	if (!REQ_ARGS(5))
		return -EINVAL;

	attach_type = parse_attach_type(*argv);
	if (attach_type == net_attach_type_size) {
		p_err("invalid net attach/detach type: %s", *argv);
		return -EINVAL;
	}
	NEXT_ARG();

	progfd = prog_parse_fd(&argc, &argv);
	if (progfd < 0)
		return -EINVAL;

	ifindex = net_parse_dev(&argc, &argv);
	if (ifindex < 1) {
		err = -EINVAL;
		goto cleanup;
	}

	if (argc) {
		if (is_prefix(*argv, "overwrite")) {
			overwrite = true;
		} else {
			p_err("expected 'overwrite', got: '%s'?", *argv);
			err = -EINVAL;
			goto cleanup;
		}
	}

	/* attach xdp prog */
	if (is_prefix("xdp", attach_type_strings[attach_type]))
		err = do_attach_detach_xdp(progfd, attach_type, ifindex,
					   overwrite);
	if (err) {
		p_err("interface %s attach failed: %s",
		      attach_type_strings[attach_type], strerror(-err));
		goto cleanup;
	}

	if (json_output)
		jsonw_null(json_wtr);
cleanup:
	close(progfd);
	return err;
}

static int do_detach(int argc, char **argv)
{
	enum net_attach_type attach_type;
	int progfd, ifindex, err = 0;

	/* parse detach args */
	if (!REQ_ARGS(3))
		return -EINVAL;

	attach_type = parse_attach_type(*argv);
	if (attach_type == net_attach_type_size) {
		p_err("invalid net attach/detach type: %s", *argv);
		return -EINVAL;
	}
	NEXT_ARG();

	ifindex = net_parse_dev(&argc, &argv);
	if (ifindex < 1)
		return -EINVAL;

	/* detach xdp prog */
	progfd = -1;
	if (is_prefix("xdp", attach_type_strings[attach_type]))
		err = do_attach_detach_xdp(progfd, attach_type, ifindex, NULL);

	if (err < 0) {
		p_err("interface %s detach failed: %s",
		      attach_type_strings[attach_type], strerror(-err));
		return err;
	}

	if (json_output)
		jsonw_null(json_wtr);

	return 0;
}

static int do_show(int argc, char **argv)
{
	struct bpf_attach_info attach_info = {};
	int i, sock, ret, filter_idx = -1;
	struct bpf_netdev_t dev_array;
	unsigned int nl_pid = 0;
	char err_buf[256];

	if (argc == 2) {
		filter_idx = net_parse_dev(&argc, &argv);
		if (filter_idx < 1)
			return -1;
	} else if (argc != 0) {
		usage();
	}

	ret = query_flow_dissector(&attach_info);
	if (ret)
		return -1;

	sock = netlink_open(&nl_pid);
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
	ret = netlink_get_link(sock, nl_pid, dump_link_nlmsg, &dev_array);
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

	NET_START_ARRAY("flow_dissector", "%s:\n");
	if (attach_info.flow_dissector_id > 0)
		NET_DUMP_UINT("id", "id %u", attach_info.flow_dissector_id);
	NET_END_ARRAY("\n");

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
		"Usage: %1$s %2$s { show | list } [dev <devname>]\n"
		"       %1$s %2$s attach ATTACH_TYPE PROG dev <devname> [ overwrite ]\n"
		"       %1$s %2$s detach ATTACH_TYPE dev <devname>\n"
		"       %1$s %2$s help\n"
		"\n"
		"       " HELP_SPEC_PROGRAM "\n"
		"       ATTACH_TYPE := { xdp | xdpgeneric | xdpdrv | xdpoffload }\n"
		"\n"
		"Note: Only xdp and tc attachments are supported now.\n"
		"      For progs attached to cgroups, use \"bpftool cgroup\"\n"
		"      to dump program attachments. For program types\n"
		"      sk_{filter,skb,msg,reuseport} and lwt/seg6, please\n"
		"      consult iproute2.\n"
		"",
		bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "show",	do_show },
	{ "list",	do_show },
	{ "attach",	do_attach },
	{ "detach",	do_detach },
	{ "help",	do_help },
	{ 0 }
};

int do_net(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
