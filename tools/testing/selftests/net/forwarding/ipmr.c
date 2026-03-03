// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */

#include <linux/if.h>
#include <linux/mroute.h>
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
};

FIXTURE_VARIANT(ipmr)
{
	int family;
	int protocol;
	int level;
	int opts[MRT_MAX - MRT_BASE + 1];
};

FIXTURE_VARIANT_ADD(ipmr, ipv4)
{
	.family = AF_INET,
	.protocol = IPPROTO_IGMP,
	.level = IPPROTO_IP,
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

static int nl_sendmsg_mfc(struct __test_metadata *_metadata, FIXTURE_DATA(ipmr) *self,
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
			.rtm_family = RTNL_FAMILY_IPMR,
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
	struct vifctl vif = {
		.vifc_vifi = 0,
		.vifc_flags = VIFF_REGISTER,
	};
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);

	err = system("cat /proc/net/ip_mr_vif | grep -q pimreg");
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_VIF - MRT_BASE],
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);
}

TEST_F(ipmr, mrt_del_vif_unreg)
{
	struct vifctl vif = {
		.vifc_vifi = 0,
		.vifc_flags = VIFF_USE_IFINDEX,
		.vifc_lcl_ifindex = self->veth_ifindex,
	};
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);

	err = system("cat /proc/net/ip_mr_vif | grep -q veth0");
	ASSERT_EQ(0, err);

	/* VIF is removed along with its device. */
	err = system("ip link del veth0");
	ASSERT_EQ(0, err);

	/* mrt->vif_table[veth_ifindex]->dev is NULL. */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_VIF - MRT_BASE],
			 &vif,  sizeof(vif));
	ASSERT_EQ(-1, err);
	ASSERT_EQ(EADDRNOTAVAIL, errno);
}

TEST_F(ipmr, mrt_del_vif_netns_dismantle)
{
	struct vifctl vif = {
		.vifc_vifi = 0,
		.vifc_flags = VIFF_USE_IFINDEX,
		.vifc_lcl_ifindex = self->veth_ifindex,
	};
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);

	/* Let cleanup_net() remove veth0 and VIF. */
}

TEST_F(ipmr, mrt_add_mfc)
{
	struct mfcctl mfc = {};
	int err;

	/* MRT_ADD_MFC / MRT_ADD_MFC_PROXY does not need vif to exist (unlike netlink). */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_MFC - MRT_BASE],
			 &mfc,  sizeof(mfc));
	ASSERT_EQ(0, err);

	/* (0.0.0.0 -> 0.0.0.0) */
	err = system("cat /proc/net/ip_mr_cache | grep -q '00000000 00000000' ");
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_MFC - MRT_BASE],
			 &mfc,  sizeof(mfc));
}

TEST_F(ipmr, mrt_add_mfc_proxy)
{
	struct mfcctl mfc = {};
	int err;

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_MFC_PROXY - MRT_BASE],
			 &mfc,  sizeof(mfc));
	ASSERT_EQ(0, err);

	err = system("cat /proc/net/ip_mr_cache | grep -q '00000000 00000000' ");
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_DEL_MFC_PROXY - MRT_BASE],
			 &mfc,  sizeof(mfc));
}

TEST_F(ipmr, mrt_add_mfc_netlink)
{
	struct vifctl vif = {
		.vifc_vifi = 0,
		.vifc_flags = VIFF_USE_IFINDEX,
		.vifc_lcl_ifindex = self->veth_ifindex,
	};
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
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	err = system("cat /proc/net/ip_mr_cache | grep -q '00000000 00000000' ");
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, RTM_DELROUTE, &mfc_attr);
	ASSERT_EQ(0, err);
}

TEST_F(ipmr, mrt_add_mfc_netlink_proxy)
{
	struct vifctl vif = {
		.vifc_vifi = 0,
		.vifc_flags = VIFF_USE_IFINDEX,
		.vifc_lcl_ifindex = self->veth_ifindex,
	};
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
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	err = system("cat /proc/net/ip_mr_cache | grep -q '00000000 00000000' ");
	ASSERT_EQ(0, err);

	err = nl_sendmsg_mfc(_metadata, self, RTM_DELROUTE, &mfc_attr);
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
	err = nl_sendmsg_mfc(_metadata, self, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(-ENFILE, err);

	/* netlink always requires RTA_IIF of an existing vif. */
	mfc_attr.ifindex = self->veth_ifindex;
	err = nl_sendmsg_mfc(_metadata, self, RTM_NEWROUTE, &mfc_attr);
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
	err = nl_sendmsg_mfc(_metadata, self, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	err = system("cat /proc/net/ip_mr_cache | grep -q '00000000 00000000' ");
	ASSERT_EQ(0, err);

	/* Remove mrt->vif_table[0]. */
	err = system("ip link del veth0");
	ASSERT_EQ(0, err);

	/* MFC entry is NOT removed even if the tied VIF is removed... */
	err = system("cat /proc/net/ip_mr_cache | grep -q '00000000 00000000' ");
	ASSERT_EQ(0, err);

	/* ... and netlink is not capable of removing such an entry
	 * because netlink always requires a valid RTA_IIF ... :/
	 */
	err = nl_sendmsg_mfc(_metadata, self, RTM_DELROUTE, &mfc_attr);
	ASSERT_EQ(-ENODEV, err);

	/* It can be removed by setsockopt(), but let cleanup_net() remove this time. */
}

TEST_F(ipmr, mrt_table_flush)
{
	struct vifctl vif = {
		.vifc_vifi = 0,
		.vifc_flags = VIFF_USE_IFINDEX,
		.vifc_lcl_ifindex = self->veth_ifindex,
	};
	struct mfc_attr mfc_attr = {
		.origin = 0,
		.group = 0,
		.ifindex = self->veth_ifindex,
		.proxy = false,
	};
	int table_id = 92;
	int err, flags;

	/* Set a random table id rather than RT_TABLE_DEFAULT.
	 * Note that /proc/net/ip_mr_{vif,cache} only supports RT_TABLE_DEFAULT.
	 */
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_TABLE - MRT_BASE],
			 &table_id,  sizeof(table_id));
	ASSERT_EQ(0, err);

	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_ADD_VIF - MRT_BASE],
			 &vif,  sizeof(vif));
	ASSERT_EQ(0, err);

	mfc_attr.table = table_id;
	err = nl_sendmsg_mfc(_metadata, self, RTM_NEWROUTE, &mfc_attr);
	ASSERT_EQ(0, err);

	/* Flush mrt->vif_table[] and all caches. */
	flags = MRT_FLUSH_VIFS | MRT_FLUSH_VIFS_STATIC |
		MRT_FLUSH_MFC | MRT_FLUSH_MFC_STATIC;
	err = setsockopt(self->raw_sk,
			 variant->level, variant->opts[MRT_FLUSH - MRT_BASE],
			 &flags,  sizeof(flags));
	ASSERT_EQ(0, err);
}

TEST_HARNESS_MAIN
