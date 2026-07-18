// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */

#include <linux/if.h>
#include <linux/in6.h>
#include <linux/mroute.h>
#include <linux/mroute6.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/socket.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "kselftest_harness.h"

FIXTURE(ipmr)
{
	int netlink_sk;
	int raw_sk;
	int veth_ifindex;
	union {
		struct vifctl vif;
		struct mif6ctl vif6;
	};
	union {
		struct mfcctl mfc;
		struct mf6cctl mfc6;
	};
};

FIXTURE_VARIANT(ipmr)
{
	int family;
	int protocol;
	int level;
	int rtm_family;
	int opts[MRT_MAX - MRT_BASE + 1];
	int flush_flags;
	int vif_size;
	char vif_check_cmd_pimreg[64];
	char vif_check_cmd_veth[64];
	int mfc_size;
	char mfc_check_cmd[1024];
};

FIXTURE_VARIANT_ADD(ipmr, ipv4)
{
	.family = AF_INET,
	.protocol = IPPROTO_IGMP,
	.level = IPPROTO_IP,
	.rtm_family = RTNL_FAMILY_IPMR,
	.opts = {
		MRT_INIT,
		MRT_DONE,
		MRT_ADD_VIF,
		MRT_DEL_VIF,
		MRT_ADD_MFC,
		MRT_DEL_MFC,
		MRT_VERSION,
		MRT_ASSERT,
		MRT_PIM,
		MRT_TABLE,
		MRT_ADD_MFC_PROXY,
		MRT_DEL_MFC_PROXY,
		MRT_FLUSH,
	},
	.flush_flags = MRT_FLUSH_MFC | MRT_FLUSH_MFC_STATIC |
		MRT_FLUSH_VIFS | MRT_FLUSH_VIFS_STATIC,
	.vif_size = sizeof(struct vifctl),
	.vif_check_cmd_pimreg = "cat /proc/net/ip_mr_vif | grep -q pimreg",
	.vif_check_cmd_veth = "cat /proc/net/ip_mr_vif | grep -q veth",
	.mfc_size = sizeof(struct mfcctl),
	.mfc_check_cmd = "cat /proc/net/ip_mr_cache | grep -q '00000000 00000000'",
};

FIXTURE_VARIANT_ADD(ipmr, ipv6)
{
	.family = AF_INET6,
	.protocol = IPPROTO_ICMPV6,
	.level = IPPROTO_IPV6,
	.rtm_family = RTNL_FAMILY_IP6MR,
	.opts = {
		MRT6_INIT,
		MRT6_DONE,
		MRT6_ADD_MIF,
		MRT6_DEL_MIF,
		MRT6_ADD_MFC,
		MRT6_DEL_MFC,
		MRT6_VERSION,
		MRT6_ASSERT,
		MRT6_PIM,
		MRT6_TABLE,
		MRT6_ADD_MFC_PROXY,
		MRT6_DEL_MFC_PROXY,
		MRT6_FLUSH,
	},
	.flush_flags = MRT6_FLUSH_MFC | MRT6_FLUSH_MFC_STATIC |
		MRT6_FLUSH_MIFS | MRT6_FLUSH_MIFS_STATIC,
	.vif_size = sizeof(struct mif6ctl),
	.vif_check_cmd_pimreg = "cat /proc/net/ip6_mr_vif | grep -q pim6reg",
	.vif_check_cmd_veth = "cat /proc/net/ip6_mr_vif | grep -q veth",
	.mfc_size = sizeof(struct mf6cctl),
	.mfc_check_cmd = "cat /proc/net/ip6_mr_cache | "
		"grep -q '0000:0000:0000:0000:0000:0000:0000:0000 0000:0000:0000:0000:0000:0000:0000:0000'",
};

struct mfc_attr {
	int table;
	__u32 origin;
	__u32 group;
	int ifindex;
	bool proxy;
};

static struct rtattr *nl_add_rtattr(struct nlmsghdr *nlmsg, struct rtattr *rta,
				    int type, const void *data, int len)
{
	int unused = 0;

	rta->rta_type = type;
	rta->rta_len = RTA_LENGTH(len);
	memcpy(RTA_DATA(rta), data, len);

	nlmsg->nlmsg_len += NLMSG_ALIGN(rta->rta_len);

	return RTA_NEXT(rta, unused);
}

static int nl_sendmsg_mfc(struct __test_metadata *_metadata,
			  FIXTURE_DATA(ipmr) *self,
			  const FIXTURE_VARIANT(ipmr) *variant,
			  __u16 nlmsg_type, struct mfc_attr *mfc_attr)
{
	struct {
		struct nlmsghdr nlmsg;
		struct rtmsg rtm;
		char buf[4096];
	} req = {
		.nlmsg = {
			.nlmsg_len = NLMSG_LENGTH(sizeof(req.rtm)),
			/* ipmr does not care about NLM_F_CREATE and NLM_F_EXCL ... */
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK,
			.nlmsg_type = nlmsg_type,
		},
		.rtm = {
			/* hard requirements in rtm_to_ipmr_mfcc() */
			.rtm_family = variant->rtm_family,
			.rtm_dst_len = 32,
			.rtm_type = RTN_MULTICAST,
			.rtm_scope = RT_SCOPE_UNIVERSE,
			.rtm_protocol = RTPROT_MROUTED,
		},
	};
	struct nlmsghdr *nlmsg = &req.nlmsg;
	struct nlmsgerr *errmsg;
	struct rtattr *rta;
	int err;

	rta = (struct rtattr *)&req.buf;
	rta = nl_add_rtattr(nlmsg, rta, RTA_TABLE, &mfc_attr->table, sizeof(mfc_attr->table));
	rta = nl_add_rtattr(nlmsg, rta, RTA_SRC, &mfc_attr->origin, sizeof(mfc_attr->origin));
	rta = nl_add_rtattr(nlmsg, rta, RTA_DST, &mfc_attr->group, sizeof(mfc_attr->group));
	if (mfc_attr->ifindex)
		rta = nl_add_rtattr(nlmsg, rta, RTA_IIF, &mfc_attr->ifindex, sizeof(mfc_attr->ifindex));
	if (mfc_attr->proxy)
		rta = nl_add_rtattr(nlmsg, rta, RTA_PREFSRC, NULL, 0);

	err = send(self->netlink_sk, &req, req.nlmsg.nlmsg_len, 0);
	ASSERT_EQ(err, req.nlmsg.nlmsg_len);

	memset(&req, 0, sizeof(req));

	err = recv(self->netlink_sk, &req, sizeof(req), 0);
	ASSERT_TRUE(NLMSG_OK(nlmsg, err));
	ASSERT_EQ(NLMSG_ERROR, nlmsg->nlmsg_type);

	errmsg = (struct nlmsgerr *)NLMSG_DATA(nlmsg);
	return errmsg->error;
}

FIXTURE_SETUP(ipmr)
{
	struct ifreq ifr = {
		.ifr_name = "veth0",
	};
	int err;

	err = unshare(CLONE_NEWNET);
	ASSERT_EQ(0, err);

	self->netlink_sk = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	ASSERT_LE(0, self->netlink_sk);

	self->raw_sk = socket(variant->family, SOCK_RAW, variant->protocol);
	ASSERT_LT(0, self->raw_sk);

	err = system("ip link add veth0 type veth peer veth1");
	ASSERT_EQ(0, err);

	err = ioctl(self->raw_sk, SIOCGIFINDEX, &ifr);
	ASSERT_EQ(0, err);

	self->veth_ifindex = ifr.ifr_ifindex;

	if (variant->family == AF_INET) {
		self->vif = (struct vifctl){
			.vifc_flags = VIFF_USE_IFINDEX,
			.vifc_lcl_ifindex = self->veth_ifindex,
		};
	} else {
		self->vif6 = (struct mif6ctl){
			.mif6c_flags = 0,
			.mif6c_pifi = self->veth_ifindex,
		};
	}
}

FIXTURE_TEARDOWN(ipmr)
{
	close(self->raw_sk);
	close(self->netlink_sk);
}

TEST_F(ipmr, mrt_init)
{
	int err, val = 0;  /* any value is ok, but size must be int for MRT_INIT. */

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_INIT - MRT_BASE],
			 &val,  sizeof(val));
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DONE - MRT_BASE],
			 &val,  sizeof(val));
	ASSERT_EQ(0, err);
}

TEST_F(ipmr, mrt_add_vif_register)
{
	int err;

	memset(&self->vif, 0, variant->vif_size);

	if (variant->family == AF_INET)
		self->vif.vifc_flags = VIFF_REGISTER;
	else
		self->vif6.mif6c_flags = MIFF_REGISTER;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &self->vif,  variant->vif_size);
	ASSERT_EQ(0, err);

	err = system(variant->vif_check_cmd_pimreg);
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_VIF - MRT_BASE],
			 &self->vif,  variant->vif_size);
	ASSERT_EQ(0, err);
}

TEST_F(ipmr, mrt_del_vif_unreg)
{
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &self->vif,  variant->vif_size);
	ASSERT_EQ(0, err);

	err = system(variant->vif_check_cmd_veth);
	ASSERT_EQ(0, err);

	/* VIF is removed along with its device. */
	err = system("ip link del veth0");
	ASSERT_EQ(0, err);

	/* mrt->vif_table[veth_ifindex]->dev is NULL. */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_VIF - MRT_BASE],
			 &self->vif,  variant->vif_size);
	ASSERT_EQ(-1, err);
	ASSERT_EQ(EADDRNOTAVAIL, errno);
}

TEST_F(ipmr, mrt_del_vif_netns_dismantle)
{
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &self->vif,  variant->vif_size);
	ASSERT_EQ(0, err);

	/* Let cleanup_net() remove veth0 and VIF. */
}

TEST_F(ipmr, mrt_add_mfc)
{
	int err;

	/* MRT_ADD_MFC / MRT_ADD_MFC_PROXY does not need vif to exist (unlike netlink). */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_MFC - MRT_BASE],
			 &self->mfc, variant->mfc_size);
	ASSERT_EQ(0, err);

	/* (0.0.0.0 -> 0.0.0.0) */
	err = system(variant->mfc_check_cmd);
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_MFC - MRT_BASE],
			 &self->mfc, variant->mfc_size);
}

TEST_F(ipmr, mrt_add_mfc_proxy)
{
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_MFC_PROXY - MRT_BASE],
			 &self->mfc, variant->mfc_size);
	ASSERT_EQ(0, err);

	err = system(variant->mfc_check_cmd);
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_MFC_PROXY - MRT_BASE],
			 &self->mfc, variant->mfc_size);
}

TEST_F(ipmr, mrt_add_mfc_netlink)
{
	struct mfc_attr mfc_attr = {
		.table = RT_TABLE_DEFAULT,
		.origin = 0,
		.group = 0,
		.ifindex = self->veth_ifindex,
		.proxy = false,
	};
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &self->vif, variant->vif_size);
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	err = system(variant->mfc_check_cmd);
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_DELROUTE, &mfc_attr);
	ASSERT_EQ(0, err);
}

TEST_F(ipmr, mrt_add_mfc_netlink_proxy)
{
	struct mfc_attr mfc_attr = {
		.table = RT_TABLE_DEFAULT,
		.origin = 0,
		.group = 0,
		.ifindex = self->veth_ifindex,
		.proxy = true,
	};
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &self->vif, variant->vif_size);
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	err = system(variant->mfc_check_cmd);
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_DELROUTE, &mfc_attr);
	ASSERT_EQ(0, err);
}

TEST_F(ipmr, mrt_add_mfc_netlink_no_vif)
{
	struct mfc_attr mfc_attr = {
		.table = RT_TABLE_DEFAULT,
		.origin = 0,
		.group = 0,
		.proxy = false,
	};
	int err;

	/* netlink always requires RTA_IIF of an existing vif. */
	mfc_attr.ifindex = 0;
	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(-ENFILE, err);

	/* netlink always requires RTA_IIF of an existing vif. */
	mfc_attr.ifindex = self->veth_ifindex;
	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(-ENFILE, err);
}

TEST_F(ipmr, mrt_del_mfc_netlink_netns_dismantle)
{
	struct vifctl vifs[2] = {
		{
			.vifc_vifi = 0,
			.vifc_flags = VIFF_USE_IFINDEX,
			.vifc_lcl_ifindex = self->veth_ifindex,
		},
		{
			.vifc_vifi = 1,
			.vifc_flags = VIFF_REGISTER,
		}
	};
	struct mfc_attr mfc_attr = {
		.table = RT_TABLE_DEFAULT,
		.origin = 0,
		.group = 0,
		.ifindex = self->veth_ifindex,
		.proxy = false,
	};
	int i, err;

	for (i = 0; i < 2; i++) {
		/* Create 2 VIFs just to avoid -ENFILE later. */
		err = setsockopt(self->raw_sk,
				 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
				 &vifs[i],  sizeof(vifs[i]));
		ASSERT_EQ(0, err);
	}

	/* Create a MFC for mrt->vif_table[0]. */
	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	err = system(variant->mfc_check_cmd);
	ASSERT_EQ(0, err);

	/* Remove mrt->vif_table[0]. */
	err = system("ip link del veth0");
	ASSERT_EQ(0, err);

	/* MFC entry is NOT removed even if the tied VIF is removed... */
	err = system(variant->mfc_check_cmd);
	ASSERT_EQ(0, err);

	/* ... and netlink is not capable of removing such an entry
	 * because netlink always requires a valid RTA_IIF ... :/
	 */
	err = nl_sendmsg_mfc(_metadata, self, variant, RTM_DELROUTE, &mfc_attr);
	ASSERT_EQ(-ENODEV, err);

	/* It can be removed by setsockopt(), but let cleanup_net() remove this time. */
}

TEST_F(ipmr, mrt_table_flush)
{
	struct mfc_attr mfc_attr = {
		.origin = 0,
		.group = 0,
		.ifindex = self->veth_ifindex,
		.proxy = false,
	};
	int table_id = 92;
	int err;

	/* Set a random table id rather than RT_TABLE_DEFAULT.
	 * Note that /proc/net/ip_mr_{vif,cache} only supports RT_TABLE_DEFAULT.
	 */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_TABLE - MRT_BASE],
			 &table_id,  sizeof(table_id));
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &self->vif,  variant->vif_size);
	ASSERT_EQ(0, err);

	if (variant->family == AF_INET) {
		mfc_attr.table = table_id;
		err = nl_sendmsg_mfc(_metadata, self, variant, RTM_NEWROUTE, &mfc_attr);
	} else {
		err = setsockopt(self->raw_sk,
				 variant->level, variant->opts[MRT_ADD_MFC - MRT_BASE],
				 &self->mfc, variant->mfc_size);
	}
	ASSERT_EQ(0, err);

	/* Flush mrt->vif_table[] and all caches. */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_FLUSH - MRT_BASE],
			 &variant->flush_flags,  sizeof(variant->flush_flags));
	ASSERT_EQ(0, err);
}

XFAIL_ADD(ipmr, ipv6, mrt_add_mfc_netlink);
XFAIL_ADD(ipmr, ipv6, mrt_add_mfc_netlink_proxy);
XFAIL_ADD(ipmr, ipv6, mrt_add_mfc_netlink_no_vif);
XFAIL_ADD(ipmr, ipv6, mrt_del_mfc_netlink_netns_dismantle);

TEST_HARNESS_MAIN
