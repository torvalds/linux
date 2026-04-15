// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <kselftest_harness.h>

#include "ovs_datapath-user.h"

static void ovs_print_datapath(struct __test_metadata *_metadata,
			       struct ovs_datapath_get_rsp *dp)
{
	EXPECT_TRUE((bool)dp->_len.name);
	if (!dp->_len.name)
		return;

	EXPECT_TRUE((bool)dp->_hdr.dp_ifindex);
	ksft_print_msg("%s(%d): pid:%u cache:%u\n",
		       dp->name, dp->_hdr.dp_ifindex,
		       dp->upcall_pid, dp->masks_cache_size);
}

FIXTURE(ovs)
{
	struct ynl_sock *ys;
	char *dp_name;
};

FIXTURE_SETUP(ovs)
{
	self->ys = ynl_sock_create(&ynl_ovs_datapath_family, NULL);
	ASSERT_NE(NULL, self->ys)
		TH_LOG("failed to create OVS datapath socket");
}

FIXTURE_TEARDOWN(ovs)
{
	if (self->dp_name) {
		struct ovs_datapath_del_req *req;

		req = ovs_datapath_del_req_alloc();
		if (req) {
			ovs_datapath_del_req_set_name(req, self->dp_name);
			ovs_datapath_del(self->ys, req);
			ovs_datapath_del_req_free(req);
		}
	}
	ynl_sock_destroy(self->ys);
}

TEST_F(ovs, crud)
{
	struct ovs_datapath_get_req_dump *dreq;
	struct ovs_datapath_new_req *new_req;
	struct ovs_datapath_get_list *dps;
	struct ovs_datapath_get_rsp *dp;
	struct ovs_datapath_get_req *req;
	bool found = false;
	int err;

	new_req = ovs_datapath_new_req_alloc();
	ASSERT_NE(NULL, new_req);
	ovs_datapath_new_req_set_upcall_pid(new_req, 1);
	ovs_datapath_new_req_set_name(new_req, "ynl-test");

	err = ovs_datapath_new(self->ys, new_req);
	ovs_datapath_new_req_free(new_req);
	ASSERT_EQ(0, err) {
		TH_LOG("new failed: %s", self->ys->err.msg);
	}
	self->dp_name = "ynl-test";

	ksft_print_msg("get:\n");
	req = ovs_datapath_get_req_alloc();
	ASSERT_NE(NULL, req);
	ovs_datapath_get_req_set_name(req, "ynl-test");

	dp = ovs_datapath_get(self->ys, req);
	ovs_datapath_get_req_free(req);
	ASSERT_NE(NULL, dp) {
		TH_LOG("get failed: %s", self->ys->err.msg);
	}

	ovs_print_datapath(_metadata, dp);
	EXPECT_STREQ("ynl-test", dp->name);
	ovs_datapath_get_rsp_free(dp);

	ksft_print_msg("dump:\n");
	dreq = ovs_datapath_get_req_dump_alloc();
	ASSERT_NE(NULL, dreq);

	dps = ovs_datapath_get_dump(self->ys, dreq);
	ovs_datapath_get_req_dump_free(dreq);
	ASSERT_NE(NULL, dps) {
		TH_LOG("dump failed: %s", self->ys->err.msg);
	}

	ynl_dump_foreach(dps, d) {
		ovs_print_datapath(_metadata, d);
		if (d->name && !strcmp(d->name, "ynl-test"))
			found = true;
	}
	ovs_datapath_get_list_free(dps);
	EXPECT_TRUE(found);
}

TEST_HARNESS_MAIN
