/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NETLINK_HELPERS_H
#define NETLINK_HELPERS_H

#include <string.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

struct rtnl_handle {
	int			fd;
	struct sockaddr_nl	local;
	struct sockaddr_nl	peer;
	__u32			seq;
	__u32			dump;
	int			proto;
	FILE			*dump_fp;
#define RTNL_HANDLE_F_LISTEN_ALL_NSID		0x01
#define RTNL_HANDLE_F_SUPPRESS_NLERR		0x02
#define RTNL_HANDLE_F_STRICT_CHK		0x04
	int			flags;
};

#define NLMSG_TAIL(nmsg) \
	((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

typedef int (*nl_ext_ack_fn_t)(const char *errmsg, uint32_t off,
			       const struct nlmsghdr *inner_nlh);

int rtnl_open(struct rtnl_handle *rth, unsigned int subscriptions)
	      __attribute__((warn_unused_result));
void rtnl_close(struct rtnl_handle *rth);
int rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n,
	      struct nlmsghdr **answer)
	      __attribute__((warn_unused_result));

int addattr(struct nlmsghdr *n, int maxlen, int type);
int addattr8(struct nlmsghdr *n, int maxlen, int type, __u8 data);
int addattr16(struct nlmsghdr *n, int maxlen, int type, __u16 data);
int addattr32(struct nlmsghdr *n, int maxlen, int type, __u32 data);
int addattr64(struct nlmsghdr *n, int maxlen, int type, __u64 data);
int addattrstrz(struct nlmsghdr *n, int maxlen, int type, const char *data);
int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data, int alen);
int addraw_l(struct nlmsghdr *n, int maxlen, const void *data, int len);
struct rtattr *addattr_nest(struct nlmsghdr *n, int maxlen, int type);
int addattr_nest_end(struct nlmsghdr *n, struct rtattr *nest);
#endif /* NETLINK_HELPERS_H */
