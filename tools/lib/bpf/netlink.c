// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2018 Facebook */

#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "nlattr.h"

/* make sure libbpf doesn't use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

typedef int (*__dump_nlmsg_t)(struct nlmsghdr *nlmsg, libbpf_dump_nlmsg_t,
			      void *cookie);

struct xdp_id_md {
	int ifindex;
	__u32 flags;
	struct xdp_link_info info;
};

int libbpf_netlink_open(__u32 *nl_pid)
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
		pr_warn("Netlink error reporting not supported\n");
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

static int bpf_netlink_recv(int sock, __u32 nl_pid, int seq,
			    __dump_nlmsg_t _fn, libbpf_dump_nlmsg_t fn,
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

static int __bpf_set_link_xdp_fd_replace(int ifindex, int fd, int old_fd,
					 __u32 flags)
{
	int sock, seq = 0, ret;
	struct nlattr *nla, *nla_xdp;
	struct {
		struct nlmsghdr  nh;
		struct ifinfomsg ifinfo;
		char             attrbuf[64];
	} req;
	__u32 nl_pid = 0;

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0)
		return sock;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_type = RTM_SETLINK;
	req.nh.nlmsg_pid = 0;
	req.nh.nlmsg_seq = ++seq;
	req.ifinfo.ifi_family = AF_UNSPEC;
	req.ifinfo.ifi_index = ifindex;

	/* started nested attribute for XDP */
	nla = (struct nlattr *)(((char *)&req)
				+ NLMSG_ALIGN(req.nh.nlmsg_len));
	nla->nla_type = NLA_F_NESTED | IFLA_XDP;
	nla->nla_len = NLA_HDRLEN;

	/* add XDP fd */
	nla_xdp = (struct nlattr *)((char *)nla + nla->nla_len);
	nla_xdp->nla_type = IFLA_XDP_FD;
	nla_xdp->nla_len = NLA_HDRLEN + sizeof(int);
	memcpy((char *)nla_xdp + NLA_HDRLEN, &fd, sizeof(fd));
	nla->nla_len += nla_xdp->nla_len;

	/* if user passed in any flags, add those too */
	if (flags) {
		nla_xdp = (struct nlattr *)((char *)nla + nla->nla_len);
		nla_xdp->nla_type = IFLA_XDP_FLAGS;
		nla_xdp->nla_len = NLA_HDRLEN + sizeof(flags);
		memcpy((char *)nla_xdp + NLA_HDRLEN, &flags, sizeof(flags));
		nla->nla_len += nla_xdp->nla_len;
	}

	if (flags & XDP_FLAGS_REPLACE) {
		nla_xdp = (struct nlattr *)((char *)nla + nla->nla_len);
		nla_xdp->nla_type = IFLA_XDP_EXPECTED_FD;
		nla_xdp->nla_len = NLA_HDRLEN + sizeof(old_fd);
		memcpy((char *)nla_xdp + NLA_HDRLEN, &old_fd, sizeof(old_fd));
		nla->nla_len += nla_xdp->nla_len;
	}

	req.nh.nlmsg_len += NLA_ALIGN(nla->nla_len);

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		ret = -errno;
		goto cleanup;
	}
	ret = bpf_netlink_recv(sock, nl_pid, seq, NULL, NULL, NULL);

cleanup:
	close(sock);
	return ret;
}

int bpf_set_link_xdp_fd_opts(int ifindex, int fd, __u32 flags,
			     const struct bpf_xdp_set_link_opts *opts)
{
	int old_fd = -1;

	if (!OPTS_VALID(opts, bpf_xdp_set_link_opts))
		return -EINVAL;

	if (OPTS_HAS(opts, old_fd)) {
		old_fd = OPTS_GET(opts, old_fd, -1);
		flags |= XDP_FLAGS_REPLACE;
	}

	return __bpf_set_link_xdp_fd_replace(ifindex, fd,
					     old_fd,
					     flags);
}

int bpf_set_link_xdp_fd(int ifindex, int fd, __u32 flags)
{
	return __bpf_set_link_xdp_fd_replace(ifindex, fd, 0, flags);
}

static int __dump_link_nlmsg(struct nlmsghdr *nlh,
			     libbpf_dump_nlmsg_t dump_link_nlmsg, void *cookie)
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

static int get_xdp_info(void *cookie, void *msg, struct nlattr **tb)
{
	struct nlattr *xdp_tb[IFLA_XDP_MAX + 1];
	struct xdp_id_md *xdp_id = cookie;
	struct ifinfomsg *ifinfo = msg;
	int ret;

	if (xdp_id->ifindex && xdp_id->ifindex != ifinfo->ifi_index)
		return 0;

	if (!tb[IFLA_XDP])
		return 0;

	ret = libbpf_nla_parse_nested(xdp_tb, IFLA_XDP_MAX, tb[IFLA_XDP], NULL);
	if (ret)
		return ret;

	if (!xdp_tb[IFLA_XDP_ATTACHED])
		return 0;

	xdp_id->info.attach_mode = libbpf_nla_getattr_u8(
		xdp_tb[IFLA_XDP_ATTACHED]);

	if (xdp_id->info.attach_mode == XDP_ATTACHED_NONE)
		return 0;

	if (xdp_tb[IFLA_XDP_PROG_ID])
		xdp_id->info.prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_PROG_ID]);

	if (xdp_tb[IFLA_XDP_SKB_PROG_ID])
		xdp_id->info.skb_prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_SKB_PROG_ID]);

	if (xdp_tb[IFLA_XDP_DRV_PROG_ID])
		xdp_id->info.drv_prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_DRV_PROG_ID]);

	if (xdp_tb[IFLA_XDP_HW_PROG_ID])
		xdp_id->info.hw_prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_HW_PROG_ID]);

	return 0;
}

int bpf_get_link_xdp_info(int ifindex, struct xdp_link_info *info,
			  size_t info_size, __u32 flags)
{
	struct xdp_id_md xdp_id = {};
	int sock, ret;
	__u32 nl_pid = 0;
	__u32 mask;

	if (flags & ~XDP_FLAGS_MASK || !info_size)
		return -EINVAL;

	/* Check whether the single {HW,DRV,SKB} mode is set */
	flags &= (XDP_FLAGS_SKB_MODE | XDP_FLAGS_DRV_MODE | XDP_FLAGS_HW_MODE);
	mask = flags - 1;
	if (flags && flags & mask)
		return -EINVAL;

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0)
		return sock;

	xdp_id.ifindex = ifindex;
	xdp_id.flags = flags;

	ret = libbpf_nl_get_link(sock, nl_pid, get_xdp_info, &xdp_id);
	if (!ret) {
		size_t sz = min(info_size, sizeof(xdp_id.info));

		memcpy(info, &xdp_id.info, sz);
		memset((void *) info + sz, 0, info_size - sz);
	}

	close(sock);
	return ret;
}

static __u32 get_xdp_id(struct xdp_link_info *info, __u32 flags)
{
	flags &= XDP_FLAGS_MODES;

	if (info->attach_mode != XDP_ATTACHED_MULTI && !flags)
		return info->prog_id;
	if (flags & XDP_FLAGS_DRV_MODE)
		return info->drv_prog_id;
	if (flags & XDP_FLAGS_HW_MODE)
		return info->hw_prog_id;
	if (flags & XDP_FLAGS_SKB_MODE)
		return info->skb_prog_id;

	return 0;
}

int bpf_get_link_xdp_id(int ifindex, __u32 *prog_id, __u32 flags)
{
	struct xdp_link_info info;
	int ret;

	ret = bpf_get_link_xdp_info(ifindex, &info, sizeof(info), flags);
	if (!ret)
		*prog_id = get_xdp_id(&info, flags);

	return ret;
}

int libbpf_nl_get_link(int sock, unsigned int nl_pid,
		       libbpf_dump_nlmsg_t dump_link_nlmsg, void *cookie)
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

	return bpf_netlink_recv(sock, nl_pid, seq, __dump_link_nlmsg,
				dump_link_nlmsg, cookie);
}

static int __dump_class_nlmsg(struct nlmsghdr *nlh,
			      libbpf_dump_nlmsg_t dump_class_nlmsg,
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

int libbpf_nl_get_class(int sock, unsigned int nl_pid, int ifindex,
			libbpf_dump_nlmsg_t dump_class_nlmsg, void *cookie)
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

	return bpf_netlink_recv(sock, nl_pid, seq, __dump_class_nlmsg,
				dump_class_nlmsg, cookie);
}

static int __dump_qdisc_nlmsg(struct nlmsghdr *nlh,
			      libbpf_dump_nlmsg_t dump_qdisc_nlmsg,
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

int libbpf_nl_get_qdisc(int sock, unsigned int nl_pid, int ifindex,
			libbpf_dump_nlmsg_t dump_qdisc_nlmsg, void *cookie)
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

	return bpf_netlink_recv(sock, nl_pid, seq, __dump_qdisc_nlmsg,
				dump_qdisc_nlmsg, cookie);
}

static int __dump_filter_nlmsg(struct nlmsghdr *nlh,
			       libbpf_dump_nlmsg_t dump_filter_nlmsg,
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

int libbpf_nl_get_filter(int sock, unsigned int nl_pid, int ifindex, int handle,
			 libbpf_dump_nlmsg_t dump_filter_nlmsg, void *cookie)
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

	return bpf_netlink_recv(sock, nl_pid, seq, __dump_filter_nlmsg,
				dump_filter_nlmsg, cookie);
}
