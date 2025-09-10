// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (C) 2018 Facebook

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <net/if.h>
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
	NET_ATTACH_TYPE_TCX_INGRESS,
	NET_ATTACH_TYPE_TCX_EGRESS,
};

static const char * const attach_type_strings[] = {
	[NET_ATTACH_TYPE_XDP]		= "xdp",
	[NET_ATTACH_TYPE_XDP_GENERIC]	= "xdpgeneric",
	[NET_ATTACH_TYPE_XDP_DRIVER]	= "xdpdrv",
	[NET_ATTACH_TYPE_XDP_OFFLOAD]	= "xdpoffload",
	[NET_ATTACH_TYPE_TCX_INGRESS]	= "tcx_ingress",
	[NET_ATTACH_TYPE_TCX_EGRESS]	= "tcx_egress",
};

static const char * const attach_loc_strings[] = {
	[BPF_TCX_INGRESS]		= "tcx/ingress",
	[BPF_TCX_EGRESS]		= "tcx/egress",
	[BPF_NETKIT_PRIMARY]		= "netkit/primary",
	[BPF_NETKIT_PEER]		= "netkit/peer",
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

		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, (unsigned int)len);
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
	struct ip_devname_ifindex *tmp;

	if (netinfo->filter_idx > 0 && netinfo->filter_idx != ifinfo->ifi_index)
		return 0;

	if (netinfo->used_len == netinfo->array_len) {
		tmp = realloc(netinfo->devices,
			(netinfo->array_len + 16) * sizeof(struct ip_devname_ifindex));
		if (!tmp)
			return -ENOMEM;

		netinfo->devices = tmp;
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
	struct tc_kind_handle *tmp;

	if (tcinfo->is_qdisc) {
		/* skip clsact qdisc */
		if (tb[TCA_KIND] &&
		    strcmp(libbpf_nla_data(tb[TCA_KIND]), "clsact") == 0)
			return 0;
		if (info->tcm_handle == 0)
			return 0;
	}

	if (tcinfo->used_len == tcinfo->array_len) {
		tmp = realloc(tcinfo->handle_array,
			(tcinfo->array_len + 16) * sizeof(struct tc_kind_handle));
		if (!tmp)
			return -ENOMEM;

		tcinfo->handle_array = tmp;
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

static int __show_dev_tc_bpf_name(__u32 id, char *name, size_t len)
{
	struct bpf_prog_info info = {};
	__u32 ilen = sizeof(info);
	int fd, ret;

	fd = bpf_prog_get_fd_by_id(id);
	if (fd < 0)
		return fd;
	ret = bpf_obj_get_info_by_fd(fd, &info, &ilen);
	if (ret < 0)
		goto out;
	ret = -ENOENT;
	if (info.name[0]) {
		get_prog_full_name(&info, fd, name, len);
		ret = 0;
	}
out:
	close(fd);
	return ret;
}

static void __show_dev_tc_bpf(const struct ip_devname_ifindex *dev,
			      const enum bpf_attach_type loc)
{
	__u32 prog_flags[64] = {}, link_flags[64] = {}, i, j;
	__u32 prog_ids[64] = {}, link_ids[64] = {};
	LIBBPF_OPTS(bpf_prog_query_opts, optq);
	char prog_name[MAX_PROG_FULL_NAME];
	int ret;

	optq.prog_ids = prog_ids;
	optq.prog_attach_flags = prog_flags;
	optq.link_ids = link_ids;
	optq.link_attach_flags = link_flags;
	optq.count = ARRAY_SIZE(prog_ids);

	ret = bpf_prog_query_opts(dev->ifindex, loc, &optq);
	if (ret)
		return;
	for (i = 0; i < optq.count; i++) {
		NET_START_OBJECT;
		NET_DUMP_STR("devname", "%s", dev->devname);
		NET_DUMP_UINT("ifindex", "(%u)", (unsigned int)dev->ifindex);
		NET_DUMP_STR("kind", " %s", attach_loc_strings[loc]);
		ret = __show_dev_tc_bpf_name(prog_ids[i], prog_name,
					     sizeof(prog_name));
		if (!ret)
			NET_DUMP_STR("name", " %s", prog_name);
		NET_DUMP_UINT("prog_id", " prog_id %u ", prog_ids[i]);
		if (prog_flags[i] || json_output) {
			NET_START_ARRAY("prog_flags", "%s ");
			for (j = 0; prog_flags[i] && j < 32; j++) {
				if (!(prog_flags[i] & (1U << j)))
					continue;
				NET_DUMP_UINT_ONLY(1U << j);
			}
			NET_END_ARRAY("");
		}
		if (link_ids[i] || json_output) {
			NET_DUMP_UINT("link_id", "link_id %u ", link_ids[i]);
			if (link_flags[i] || json_output) {
				NET_START_ARRAY("link_flags", "%s ");
				for (j = 0; link_flags[i] && j < 32; j++) {
					if (!(link_flags[i] & (1U << j)))
						continue;
					NET_DUMP_UINT_ONLY(1U << j);
				}
				NET_END_ARRAY("");
			}
		}
		NET_END_OBJECT_FINAL;
	}
}

static void show_dev_tc_bpf(struct ip_devname_ifindex *dev)
{
	__show_dev_tc_bpf(dev, BPF_TCX_INGRESS);
	__show_dev_tc_bpf(dev, BPF_TCX_EGRESS);

	__show_dev_tc_bpf(dev, BPF_NETKIT_PRIMARY);
	__show_dev_tc_bpf(dev, BPF_NETKIT_PEER);
}

static int show_dev_tc_bpf_classic(int sock, unsigned int nl_pid,
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

	return bpf_xdp_attach(ifindex, progfd, flags, NULL);
}

static int get_tcx_type(enum net_attach_type attach_type)
{
	switch (attach_type) {
	case NET_ATTACH_TYPE_TCX_INGRESS:
		return BPF_TCX_INGRESS;
	case NET_ATTACH_TYPE_TCX_EGRESS:
		return BPF_TCX_EGRESS;
	default:
		return -1;
	}
}

static int do_attach_tcx(int progfd, enum net_attach_type attach_type, int ifindex)
{
	int type = get_tcx_type(attach_type);

	return bpf_prog_attach(progfd, ifindex, type, 0);
}

static int do_detach_tcx(int targetfd, enum net_attach_type attach_type)
{
	int type = get_tcx_type(attach_type);

	return bpf_prog_detach(targetfd, type);
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

	switch (attach_type) {
	/* attach xdp prog */
	case NET_ATTACH_TYPE_XDP:
	case NET_ATTACH_TYPE_XDP_GENERIC:
	case NET_ATTACH_TYPE_XDP_DRIVER:
	case NET_ATTACH_TYPE_XDP_OFFLOAD:
		err = do_attach_detach_xdp(progfd, attach_type, ifindex, overwrite);
		break;
	/* attach tcx prog */
	case NET_ATTACH_TYPE_TCX_INGRESS:
	case NET_ATTACH_TYPE_TCX_EGRESS:
		err = do_attach_tcx(progfd, attach_type, ifindex);
		break;
	default:
		break;
	}

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

	switch (attach_type) {
	/* detach xdp prog */
	case NET_ATTACH_TYPE_XDP:
	case NET_ATTACH_TYPE_XDP_GENERIC:
	case NET_ATTACH_TYPE_XDP_DRIVER:
	case NET_ATTACH_TYPE_XDP_OFFLOAD:
		progfd = -1;
		err = do_attach_detach_xdp(progfd, attach_type, ifindex, NULL);
		break;
	/* detach tcx prog */
	case NET_ATTACH_TYPE_TCX_INGRESS:
	case NET_ATTACH_TYPE_TCX_EGRESS:
		err = do_detach_tcx(ifindex, attach_type);
		break;
	default:
		break;
	}

	if (err < 0) {
		p_err("interface %s detach failed: %s",
		      attach_type_strings[attach_type], strerror(-err));
		return err;
	}

	if (json_output)
		jsonw_null(json_wtr);

	return 0;
}

static int netfilter_link_compar(const void *a, const void *b)
{
	const struct bpf_link_info *nfa = a;
	const struct bpf_link_info *nfb = b;
	int delta;

	delta = nfa->netfilter.pf - nfb->netfilter.pf;
	if (delta)
		return delta;

	delta = nfa->netfilter.hooknum - nfb->netfilter.hooknum;
	if (delta)
		return delta;

	if (nfa->netfilter.priority < nfb->netfilter.priority)
		return -1;
	if (nfa->netfilter.priority > nfb->netfilter.priority)
		return 1;

	return nfa->netfilter.flags - nfb->netfilter.flags;
}

static void show_link_netfilter(void)
{
	unsigned int nf_link_len = 0, nf_link_count = 0;
	struct bpf_link_info *nf_link_info = NULL;
	__u32 id = 0;

	while (true) {
		struct bpf_link_info info;
		int fd, err;
		__u32 len;

		err = bpf_link_get_next_id(id, &id);
		if (err) {
			if (errno == ENOENT)
				break;
			p_err("can't get next link: %s (id %u)", strerror(errno), id);
			break;
		}

		fd = bpf_link_get_fd_by_id(id);
		if (fd < 0) {
			p_err("can't get link by id (%u): %s", id, strerror(errno));
			continue;
		}

		memset(&info, 0, sizeof(info));
		len = sizeof(info);

		err = bpf_link_get_info_by_fd(fd, &info, &len);

		close(fd);

		if (err) {
			p_err("can't get link info for fd %d: %s", fd, strerror(errno));
			continue;
		}

		if (info.type != BPF_LINK_TYPE_NETFILTER)
			continue;

		if (nf_link_count >= nf_link_len) {
			static const unsigned int max_link_count = INT_MAX / sizeof(info);
			struct bpf_link_info *expand;

			if (nf_link_count > max_link_count) {
				p_err("cannot handle more than %u links\n", max_link_count);
				break;
			}

			nf_link_len += 16;

			expand = realloc(nf_link_info, nf_link_len * sizeof(info));
			if (!expand) {
				p_err("realloc: %s",  strerror(errno));
				break;
			}

			nf_link_info = expand;
		}

		nf_link_info[nf_link_count] = info;
		nf_link_count++;
	}

	if (!nf_link_info)
		return;

	qsort(nf_link_info, nf_link_count, sizeof(*nf_link_info), netfilter_link_compar);

	for (id = 0; id < nf_link_count; id++) {
		NET_START_OBJECT;
		if (json_output)
			netfilter_dump_json(&nf_link_info[id], json_wtr);
		else
			netfilter_dump_plain(&nf_link_info[id]);

		NET_DUMP_UINT("id", " prog_id %u", nf_link_info[id].prog_id);
		NET_END_OBJECT;
	}

	free(nf_link_info);
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
			show_dev_tc_bpf(&dev_array.devices[i]);
			ret = show_dev_tc_bpf_classic(sock, nl_pid,
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

	NET_START_ARRAY("netfilter", "%s:\n");
	show_link_netfilter();
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
		"       ATTACH_TYPE := { xdp | xdpgeneric | xdpdrv | xdpoffload | tcx_ingress\n"
		"                        | tcx_egress }\n"
		"       " HELP_SPEC_OPTIONS " }\n"
		"\n"
		"Note: Only xdp, tcx, tc, netkit, flow_dissector and netfilter attachments\n"
		"      are currently supported.\n"
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
