// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <net/if.h>

#include <kselftest_harness.h>

#include "netdev-user.h"
#include "rt-link-user.h"

static void netdev_print_device(struct __test_metadata *_metadata,
				struct netdev_dev_get_rsp *d, unsigned int op)
{
	char ifname[IF_NAMESIZE];
	const char *name;

	EXPECT_TRUE((bool)d->_present.ifindex);
	if (!d->_present.ifindex)
		return;

	name = if_indextoname(d->ifindex, ifname);
	EXPECT_TRUE((bool)name);
	if (name)
		ksft_print_msg("%8s[%d]\t", name, d->ifindex);
	else
		ksft_print_msg("[%d]\t", d->ifindex);

	EXPECT_TRUE((bool)d->_present.xdp_features);
	if (!d->_present.xdp_features)
		return;

	printf("xdp-features (%llx):", d->xdp_features);
	for (int i = 0; d->xdp_features >= 1U << i; i++) {
		if (d->xdp_features & (1U << i))
			printf(" %s", netdev_xdp_act_str(1 << i));
	}

	printf(" xdp-rx-metadata-features (%llx):",
	       d->xdp_rx_metadata_features);
	for (int i = 0; d->xdp_rx_metadata_features >= 1U << i; i++) {
		if (d->xdp_rx_metadata_features & (1U << i))
			printf(" %s",
			       netdev_xdp_rx_metadata_str(1 << i));
	}

	printf(" xsk-features (%llx):", d->xsk_features);
	for (int i = 0; d->xsk_features >= 1U << i; i++) {
		if (d->xsk_features & (1U << i))
			printf(" %s", netdev_xsk_flags_str(1 << i));
	}

	printf(" xdp-zc-max-segs=%u", d->xdp_zc_max_segs);

	name = netdev_op_str(op);
	if (name)
		printf(" (ntf: %s)", name);
	printf("\n");
}

static int veth_create(struct ynl_sock *ys_link)
{
	struct rt_link_getlink_ntf *ntf_gl;
	struct rt_link_newlink_req *req;
	struct ynl_ntf_base_type *ntf;
	int ret;

	req = rt_link_newlink_req_alloc();
	if (!req)
		return -1;

	rt_link_newlink_req_set_nlflags(req, NLM_F_CREATE | NLM_F_ECHO);
	rt_link_newlink_req_set_linkinfo_kind(req, "veth");

	ret = rt_link_newlink(ys_link, req);
	rt_link_newlink_req_free(req);
	if (ret)
		return -1;

	if (!ynl_has_ntf(ys_link))
		return 0;

	ntf = ynl_ntf_dequeue(ys_link);
	if (!ntf || ntf->cmd != RTM_NEWLINK) {
		ynl_ntf_free(ntf);
		return 0;
	}
	ntf_gl = (void *)ntf;
	ret = ntf_gl->obj._hdr.ifi_index;
	ynl_ntf_free(ntf);

	return ret;
}

static void veth_delete(struct __test_metadata *_metadata,
			struct ynl_sock *ys_link, int ifindex)
{
	struct rt_link_dellink_req *req;

	req = rt_link_dellink_req_alloc();
	ASSERT_NE(NULL, req);

	req->_hdr.ifi_index = ifindex;
	EXPECT_EQ(0, rt_link_dellink(ys_link, req));
	rt_link_dellink_req_free(req);
}

FIXTURE(netdev)
{
	struct ynl_sock *ys;
	struct ynl_sock *ys_link;
};

FIXTURE_SETUP(netdev)
{
	struct ynl_error yerr;

	self->ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	ASSERT_NE(NULL, self->ys) {
		TH_LOG("Failed to create YNL netdev socket: %s", yerr.msg);
	}
}

FIXTURE_TEARDOWN(netdev)
{
	if (self->ys_link)
		ynl_sock_destroy(self->ys_link);
	ynl_sock_destroy(self->ys);
}

TEST_F(netdev, dump)
{
	struct netdev_dev_get_list *devs;

	devs = netdev_dev_get_dump(self->ys);
	ASSERT_NE(NULL, devs) {
		TH_LOG("dump failed: %s", self->ys->err.msg);
	}

	if (ynl_dump_empty(devs)) {
		netdev_dev_get_list_free(devs);
		SKIP(return, "no entries in dump");
	}

	ynl_dump_foreach(devs, d)
		netdev_print_device(_metadata, d, 0);

	netdev_dev_get_list_free(devs);
}

TEST_F(netdev, get)
{
	struct netdev_dev_get_list *devs;
	struct netdev_dev_get_req *req;
	struct netdev_dev_get_rsp *dev;
	int ifindex = 0;

	devs = netdev_dev_get_dump(self->ys);
	ASSERT_NE(NULL, devs) {
		TH_LOG("dump failed: %s", self->ys->err.msg);
	}

	ynl_dump_foreach(devs, d) {
		if (d->_present.ifindex) {
			ifindex = d->ifindex;
			break;
		}
	}
	netdev_dev_get_list_free(devs);

	if (!ifindex)
		SKIP(return, "no device to query");

	req = netdev_dev_get_req_alloc();
	ASSERT_NE(NULL, req);
	netdev_dev_get_req_set_ifindex(req, ifindex);

	dev = netdev_dev_get(self->ys, req);
	netdev_dev_get_req_free(req);
	ASSERT_NE(NULL, dev) {
		TH_LOG("dev_get failed: %s", self->ys->err.msg);
	}

	netdev_print_device(_metadata, dev, 0);
	netdev_dev_get_rsp_free(dev);
}

TEST_F(netdev, ntf_check)
{
	struct ynl_ntf_base_type *ntf;
	int veth_ifindex;
	bool received;
	int ret;

	ret = ynl_subscribe(self->ys, "mgmt");
	ASSERT_EQ(0, ret) {
		TH_LOG("subscribe failed: %s", self->ys->err.msg);
	}

	self->ys_link = ynl_sock_create(&ynl_rt_link_family, NULL);
	ASSERT_NE(NULL, self->ys_link)
		TH_LOG("failed to create rt-link socket");

	veth_ifindex = veth_create(self->ys_link);
	ASSERT_GT(veth_ifindex, 0)
		TH_LOG("failed to create veth");

	ynl_ntf_check(self->ys);

	ntf = ynl_ntf_dequeue(self->ys);
	received = ntf;
	if (ntf) {
		netdev_print_device(_metadata,
				    (struct netdev_dev_get_rsp *)&ntf->data,
				    ntf->cmd);
		ynl_ntf_free(ntf);
	}

	/* Drain any remaining notifications */
	while ((ntf = ynl_ntf_dequeue(self->ys)))
		ynl_ntf_free(ntf);

	veth_delete(_metadata, self->ys_link, veth_ifindex);

	ASSERT_TRUE(received)
		TH_LOG("no notification received");
}

TEST_HARNESS_MAIN
