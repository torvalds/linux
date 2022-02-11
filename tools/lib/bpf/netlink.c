// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2018 Facebook */

#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "nlattr.h"

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

typedef int (*libbpf_dump_nlmsg_t)(void *cookie, void *msg, struct nlattr **tb);

typedef int (*__dump_nlmsg_t)(struct nlmsghdr *nlmsg, libbpf_dump_nlmsg_t,
			      void *cookie);

struct xdp_id_md {
	int ifindex;
	__u32 flags;
	struct xdp_link_info info;
};

static int libbpf_netlink_open(__u32 *nl_pid)
{
	struct sockaddr_nl sa;
	socklen_t addrlen;
	int one = 1, ret;
	int sock;

	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;

	sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
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

static void libbpf_netlink_close(int sock)
{
	close(sock);
}

enum {
	NL_CONT,
	NL_NEXT,
	NL_DONE,
};

static int netlink_recvmsg(int sock, struct msghdr *mhdr, int flags)
{
	int len;

	do {
		len = recvmsg(sock, mhdr, flags);
	} while (len < 0 && (errno == EINTR || errno == EAGAIN));

	if (len < 0)
		return -errno;
	return len;
}

static int alloc_iov(struct iovec *iov, int len)
{
	void *nbuf;

	nbuf = realloc(iov->iov_base, len);
	if (!nbuf)
		return -ENOMEM;

	iov->iov_base = nbuf;
	iov->iov_len = len;
	return 0;
}

static int libbpf_netlink_recv(int sock, __u32 nl_pid, int seq,
			       __dump_nlmsg_t _fn, libbpf_dump_nlmsg_t fn,
			       void *cookie)
{
	struct iovec iov = {};
	struct msghdr mhdr = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	bool multipart = true;
	struct nlmsgerr *err;
	struct nlmsghdr *nh;
	int len, ret;

	ret = alloc_iov(&iov, 4096);
	if (ret)
		goto done;

	while (multipart) {
start:
		multipart = false;
		len = netlink_recvmsg(sock, &mhdr, MSG_PEEK | MSG_TRUNC);
		if (len < 0) {
			ret = len;
			goto done;
		}

		if (len > iov.iov_len) {
			ret = alloc_iov(&iov, len);
			if (ret)
				goto done;
		}

		len = netlink_recvmsg(sock, &mhdr, 0);
		if (len < 0) {
			ret = len;
			goto done;
		}

		if (len == 0)
			break;

		for (nh = (struct nlmsghdr *)iov.iov_base; NLMSG_OK(nh, len);
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
				switch (ret) {
				case NL_CONT:
					break;
				case NL_NEXT:
					goto start;
				case NL_DONE:
					return 0;
				default:
					return ret;
				}
			}
		}
	}
	ret = 0;
done:
	free(iov.iov_base);
	return ret;
}

static int libbpf_netlink_send_recv(struct libbpf_nla_req *req,
				    __dump_nlmsg_t parse_msg,
				    libbpf_dump_nlmsg_t parse_attr,
				    void *cookie)
{
	__u32 nl_pid = 0;
	int sock, ret;

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0)
		return sock;

	req->nh.nlmsg_pid = 0;
	req->nh.nlmsg_seq = time(NULL);

	if (send(sock, req, req->nh.nlmsg_len, 0) < 0) {
		ret = -errno;
		goto out;
	}

	ret = libbpf_netlink_recv(sock, nl_pid, req->nh.nlmsg_seq,
				  parse_msg, parse_attr, cookie);
out:
	libbpf_netlink_close(sock);
	return ret;
}

static int __bpf_set_link_xdp_fd_replace(int ifindex, int fd, int old_fd,
					 __u32 flags)
{
	struct nlattr *nla;
	int ret;
	struct libbpf_nla_req req;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len      = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nh.nlmsg_flags    = NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_type     = RTM_SETLINK;
	req.ifinfo.ifi_family = AF_UNSPEC;
	req.ifinfo.ifi_index  = ifindex;

	nla = nlattr_begin_nested(&req, IFLA_XDP);
	if (!nla)
		return -EMSGSIZE;
	ret = nlattr_add(&req, IFLA_XDP_FD, &fd, sizeof(fd));
	if (ret < 0)
		return ret;
	if (flags) {
		ret = nlattr_add(&req, IFLA_XDP_FLAGS, &flags, sizeof(flags));
		if (ret < 0)
			return ret;
	}
	if (flags & XDP_FLAGS_REPLACE) {
		ret = nlattr_add(&req, IFLA_XDP_EXPECTED_FD, &old_fd,
				 sizeof(old_fd));
		if (ret < 0)
			return ret;
	}
	nlattr_end_nested(&req, nla);

	return libbpf_netlink_send_recv(&req, NULL, NULL, NULL);
}

int bpf_set_link_xdp_fd_opts(int ifindex, int fd, __u32 flags,
			     const struct bpf_xdp_set_link_opts *opts)
{
	int old_fd = -1, ret;

	if (!OPTS_VALID(opts, bpf_xdp_set_link_opts))
		return libbpf_err(-EINVAL);

	if (OPTS_HAS(opts, old_fd)) {
		old_fd = OPTS_GET(opts, old_fd, -1);
		flags |= XDP_FLAGS_REPLACE;
	}

	ret = __bpf_set_link_xdp_fd_replace(ifindex, fd, old_fd, flags);
	return libbpf_err(ret);
}

int bpf_set_link_xdp_fd(int ifindex, int fd, __u32 flags)
{
	int ret;

	ret = __bpf_set_link_xdp_fd_replace(ifindex, fd, 0, flags);
	return libbpf_err(ret);
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
	__u32 mask;
	int ret;
	struct libbpf_nla_req req = {
		.nh.nlmsg_len      = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
		.nh.nlmsg_type     = RTM_GETLINK,
		.nh.nlmsg_flags    = NLM_F_DUMP | NLM_F_REQUEST,
		.ifinfo.ifi_family = AF_PACKET,
	};

	if (flags & ~XDP_FLAGS_MASK || !info_size)
		return libbpf_err(-EINVAL);

	/* Check whether the single {HW,DRV,SKB} mode is set */
	flags &= (XDP_FLAGS_SKB_MODE | XDP_FLAGS_DRV_MODE | XDP_FLAGS_HW_MODE);
	mask = flags - 1;
	if (flags && flags & mask)
		return libbpf_err(-EINVAL);

	xdp_id.ifindex = ifindex;
	xdp_id.flags = flags;

	ret = libbpf_netlink_send_recv(&req, __dump_link_nlmsg,
				       get_xdp_info, &xdp_id);
	if (!ret) {
		size_t sz = min(info_size, sizeof(xdp_id.info));

		memcpy(info, &xdp_id.info, sz);
		memset((void *) info + sz, 0, info_size - sz);
	}

	return libbpf_err(ret);
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

	return libbpf_err(ret);
}

typedef int (*qdisc_config_t)(struct libbpf_nla_req *req);

static int clsact_config(struct libbpf_nla_req *req)
{
	req->tc.tcm_parent = TC_H_CLSACT;
	req->tc.tcm_handle = TC_H_MAKE(TC_H_CLSACT, 0);

	return nlattr_add(req, TCA_KIND, "clsact", sizeof("clsact"));
}

static int attach_point_to_config(struct bpf_tc_hook *hook,
				  qdisc_config_t *config)
{
	switch (OPTS_GET(hook, attach_point, 0)) {
	case BPF_TC_INGRESS:
	case BPF_TC_EGRESS:
	case BPF_TC_INGRESS | BPF_TC_EGRESS:
		if (OPTS_GET(hook, parent, 0))
			return -EINVAL;
		*config = &clsact_config;
		return 0;
	case BPF_TC_CUSTOM:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int tc_get_tcm_parent(enum bpf_tc_attach_point attach_point,
			     __u32 *parent)
{
	switch (attach_point) {
	case BPF_TC_INGRESS:
	case BPF_TC_EGRESS:
		if (*parent)
			return -EINVAL;
		*parent = TC_H_MAKE(TC_H_CLSACT,
				    attach_point == BPF_TC_INGRESS ?
				    TC_H_MIN_INGRESS : TC_H_MIN_EGRESS);
		break;
	case BPF_TC_CUSTOM:
		if (!*parent)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int tc_qdisc_modify(struct bpf_tc_hook *hook, int cmd, int flags)
{
	qdisc_config_t config;
	int ret;
	struct libbpf_nla_req req;

	ret = attach_point_to_config(hook, &config);
	if (ret < 0)
		return ret;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
	req.nh.nlmsg_type  = cmd;
	req.tc.tcm_family  = AF_UNSPEC;
	req.tc.tcm_ifindex = OPTS_GET(hook, ifindex, 0);

	ret = config(&req);
	if (ret < 0)
		return ret;

	return libbpf_netlink_send_recv(&req, NULL, NULL, NULL);
}

static int tc_qdisc_create_excl(struct bpf_tc_hook *hook)
{
	return tc_qdisc_modify(hook, RTM_NEWQDISC, NLM_F_CREATE | NLM_F_EXCL);
}

static int tc_qdisc_delete(struct bpf_tc_hook *hook)
{
	return tc_qdisc_modify(hook, RTM_DELQDISC, 0);
}

int bpf_tc_hook_create(struct bpf_tc_hook *hook)
{
	int ret;

	if (!hook || !OPTS_VALID(hook, bpf_tc_hook) ||
	    OPTS_GET(hook, ifindex, 0) <= 0)
		return libbpf_err(-EINVAL);

	ret = tc_qdisc_create_excl(hook);
	return libbpf_err(ret);
}

static int __bpf_tc_detach(const struct bpf_tc_hook *hook,
			   const struct bpf_tc_opts *opts,
			   const bool flush);

int bpf_tc_hook_destroy(struct bpf_tc_hook *hook)
{
	if (!hook || !OPTS_VALID(hook, bpf_tc_hook) ||
	    OPTS_GET(hook, ifindex, 0) <= 0)
		return libbpf_err(-EINVAL);

	switch (OPTS_GET(hook, attach_point, 0)) {
	case BPF_TC_INGRESS:
	case BPF_TC_EGRESS:
		return libbpf_err(__bpf_tc_detach(hook, NULL, true));
	case BPF_TC_INGRESS | BPF_TC_EGRESS:
		return libbpf_err(tc_qdisc_delete(hook));
	case BPF_TC_CUSTOM:
		return libbpf_err(-EOPNOTSUPP);
	default:
		return libbpf_err(-EINVAL);
	}
}

struct bpf_cb_ctx {
	struct bpf_tc_opts *opts;
	bool processed;
};

static int __get_tc_info(void *cookie, struct tcmsg *tc, struct nlattr **tb,
			 bool unicast)
{
	struct nlattr *tbb[TCA_BPF_MAX + 1];
	struct bpf_cb_ctx *info = cookie;

	if (!info || !info->opts)
		return -EINVAL;
	if (unicast && info->processed)
		return -EINVAL;
	if (!tb[TCA_OPTIONS])
		return NL_CONT;

	libbpf_nla_parse_nested(tbb, TCA_BPF_MAX, tb[TCA_OPTIONS], NULL);
	if (!tbb[TCA_BPF_ID])
		return -EINVAL;

	OPTS_SET(info->opts, prog_id, libbpf_nla_getattr_u32(tbb[TCA_BPF_ID]));
	OPTS_SET(info->opts, handle, tc->tcm_handle);
	OPTS_SET(info->opts, priority, TC_H_MAJ(tc->tcm_info) >> 16);

	info->processed = true;
	return unicast ? NL_NEXT : NL_DONE;
}

static int get_tc_info(struct nlmsghdr *nh, libbpf_dump_nlmsg_t fn,
		       void *cookie)
{
	struct tcmsg *tc = NLMSG_DATA(nh);
	struct nlattr *tb[TCA_MAX + 1];

	libbpf_nla_parse(tb, TCA_MAX,
			 (struct nlattr *)((void *)tc + NLMSG_ALIGN(sizeof(*tc))),
			 NLMSG_PAYLOAD(nh, sizeof(*tc)), NULL);
	if (!tb[TCA_KIND])
		return NL_CONT;
	return __get_tc_info(cookie, tc, tb, nh->nlmsg_flags & NLM_F_ECHO);
}

static int tc_add_fd_and_name(struct libbpf_nla_req *req, int fd)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	char name[256];
	int len, ret;

	ret = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (ret < 0)
		return ret;

	ret = nlattr_add(req, TCA_BPF_FD, &fd, sizeof(fd));
	if (ret < 0)
		return ret;
	len = snprintf(name, sizeof(name), "%s:[%u]", info.name, info.id);
	if (len < 0)
		return -errno;
	if (len >= sizeof(name))
		return -ENAMETOOLONG;
	return nlattr_add(req, TCA_BPF_NAME, name, len + 1);
}

int bpf_tc_attach(const struct bpf_tc_hook *hook, struct bpf_tc_opts *opts)
{
	__u32 protocol, bpf_flags, handle, priority, parent, prog_id, flags;
	int ret, ifindex, attach_point, prog_fd;
	struct bpf_cb_ctx info = {};
	struct libbpf_nla_req req;
	struct nlattr *nla;

	if (!hook || !opts ||
	    !OPTS_VALID(hook, bpf_tc_hook) ||
	    !OPTS_VALID(opts, bpf_tc_opts))
		return libbpf_err(-EINVAL);

	ifindex      = OPTS_GET(hook, ifindex, 0);
	parent       = OPTS_GET(hook, parent, 0);
	attach_point = OPTS_GET(hook, attach_point, 0);

	handle       = OPTS_GET(opts, handle, 0);
	priority     = OPTS_GET(opts, priority, 0);
	prog_fd      = OPTS_GET(opts, prog_fd, 0);
	prog_id      = OPTS_GET(opts, prog_id, 0);
	flags        = OPTS_GET(opts, flags, 0);

	if (ifindex <= 0 || !prog_fd || prog_id)
		return libbpf_err(-EINVAL);
	if (priority > UINT16_MAX)
		return libbpf_err(-EINVAL);
	if (flags & ~BPF_TC_F_REPLACE)
		return libbpf_err(-EINVAL);

	flags = (flags & BPF_TC_F_REPLACE) ? NLM_F_REPLACE : NLM_F_EXCL;
	protocol = ETH_P_ALL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE |
			     NLM_F_ECHO | flags;
	req.nh.nlmsg_type  = RTM_NEWTFILTER;
	req.tc.tcm_family  = AF_UNSPEC;
	req.tc.tcm_ifindex = ifindex;
	req.tc.tcm_handle  = handle;
	req.tc.tcm_info    = TC_H_MAKE(priority << 16, htons(protocol));

	ret = tc_get_tcm_parent(attach_point, &parent);
	if (ret < 0)
		return libbpf_err(ret);
	req.tc.tcm_parent = parent;

	ret = nlattr_add(&req, TCA_KIND, "bpf", sizeof("bpf"));
	if (ret < 0)
		return libbpf_err(ret);
	nla = nlattr_begin_nested(&req, TCA_OPTIONS);
	if (!nla)
		return libbpf_err(-EMSGSIZE);
	ret = tc_add_fd_and_name(&req, prog_fd);
	if (ret < 0)
		return libbpf_err(ret);
	bpf_flags = TCA_BPF_FLAG_ACT_DIRECT;
	ret = nlattr_add(&req, TCA_BPF_FLAGS, &bpf_flags, sizeof(bpf_flags));
	if (ret < 0)
		return libbpf_err(ret);
	nlattr_end_nested(&req, nla);

	info.opts = opts;

	ret = libbpf_netlink_send_recv(&req, get_tc_info, NULL, &info);
	if (ret < 0)
		return libbpf_err(ret);
	if (!info.processed)
		return libbpf_err(-ENOENT);
	return ret;
}

static int __bpf_tc_detach(const struct bpf_tc_hook *hook,
			   const struct bpf_tc_opts *opts,
			   const bool flush)
{
	__u32 protocol = 0, handle, priority, parent, prog_id, flags;
	int ret, ifindex, attach_point, prog_fd;
	struct libbpf_nla_req req;

	if (!hook ||
	    !OPTS_VALID(hook, bpf_tc_hook) ||
	    !OPTS_VALID(opts, bpf_tc_opts))
		return -EINVAL;

	ifindex      = OPTS_GET(hook, ifindex, 0);
	parent       = OPTS_GET(hook, parent, 0);
	attach_point = OPTS_GET(hook, attach_point, 0);

	handle       = OPTS_GET(opts, handle, 0);
	priority     = OPTS_GET(opts, priority, 0);
	prog_fd      = OPTS_GET(opts, prog_fd, 0);
	prog_id      = OPTS_GET(opts, prog_id, 0);
	flags        = OPTS_GET(opts, flags, 0);

	if (ifindex <= 0 || flags || prog_fd || prog_id)
		return -EINVAL;
	if (priority > UINT16_MAX)
		return -EINVAL;
	if (!flush) {
		if (!handle || !priority)
			return -EINVAL;
		protocol = ETH_P_ALL;
	} else {
		if (handle || priority)
			return -EINVAL;
	}

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_type  = RTM_DELTFILTER;
	req.tc.tcm_family  = AF_UNSPEC;
	req.tc.tcm_ifindex = ifindex;
	if (!flush) {
		req.tc.tcm_handle = handle;
		req.tc.tcm_info   = TC_H_MAKE(priority << 16, htons(protocol));
	}

	ret = tc_get_tcm_parent(attach_point, &parent);
	if (ret < 0)
		return ret;
	req.tc.tcm_parent = parent;

	if (!flush) {
		ret = nlattr_add(&req, TCA_KIND, "bpf", sizeof("bpf"));
		if (ret < 0)
			return ret;
	}

	return libbpf_netlink_send_recv(&req, NULL, NULL, NULL);
}

int bpf_tc_detach(const struct bpf_tc_hook *hook,
		  const struct bpf_tc_opts *opts)
{
	int ret;

	if (!opts)
		return libbpf_err(-EINVAL);

	ret = __bpf_tc_detach(hook, opts, false);
	return libbpf_err(ret);
}

int bpf_tc_query(const struct bpf_tc_hook *hook, struct bpf_tc_opts *opts)
{
	__u32 protocol, handle, priority, parent, prog_id, flags;
	int ret, ifindex, attach_point, prog_fd;
	struct bpf_cb_ctx info = {};
	struct libbpf_nla_req req;

	if (!hook || !opts ||
	    !OPTS_VALID(hook, bpf_tc_hook) ||
	    !OPTS_VALID(opts, bpf_tc_opts))
		return libbpf_err(-EINVAL);

	ifindex      = OPTS_GET(hook, ifindex, 0);
	parent       = OPTS_GET(hook, parent, 0);
	attach_point = OPTS_GET(hook, attach_point, 0);

	handle       = OPTS_GET(opts, handle, 0);
	priority     = OPTS_GET(opts, priority, 0);
	prog_fd      = OPTS_GET(opts, prog_fd, 0);
	prog_id      = OPTS_GET(opts, prog_id, 0);
	flags        = OPTS_GET(opts, flags, 0);

	if (ifindex <= 0 || flags || prog_fd || prog_id ||
	    !handle || !priority)
		return libbpf_err(-EINVAL);
	if (priority > UINT16_MAX)
		return libbpf_err(-EINVAL);

	protocol = ETH_P_ALL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST;
	req.nh.nlmsg_type  = RTM_GETTFILTER;
	req.tc.tcm_family  = AF_UNSPEC;
	req.tc.tcm_ifindex = ifindex;
	req.tc.tcm_handle  = handle;
	req.tc.tcm_info    = TC_H_MAKE(priority << 16, htons(protocol));

	ret = tc_get_tcm_parent(attach_point, &parent);
	if (ret < 0)
		return libbpf_err(ret);
	req.tc.tcm_parent = parent;

	ret = nlattr_add(&req, TCA_KIND, "bpf", sizeof("bpf"));
	if (ret < 0)
		return libbpf_err(ret);

	info.opts = opts;

	ret = libbpf_netlink_send_recv(&req, get_tc_info, NULL, &info);
	if (ret < 0)
		return libbpf_err(ret);
	if (!info.processed)
		return libbpf_err(-ENOENT);
	return ret;
}
