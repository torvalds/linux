// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <kselftest_harness.h>

#include "devlink-user.h"

FIXTURE(devlink)
{
	struct ynl_sock *ys;
};

FIXTURE_SETUP(devlink)
{
	self->ys = ynl_sock_create(&ynl_devlink_family, NULL);
	ASSERT_NE(NULL, self->ys)
		TH_LOG("failed to create devlink socket");
}

FIXTURE_TEARDOWN(devlink)
{
	ynl_sock_destroy(self->ys);
}

TEST_F(devlink, dump)
{
	struct devlink_get_list *devs;

	devs = devlink_get_dump(self->ys);
	ASSERT_NE(NULL, devs) {
		TH_LOG("dump failed: %s", self->ys->err.msg);
	}

	if (ynl_dump_empty(devs)) {
		devlink_get_list_free(devs);
		SKIP(return, "no entries in dump");
	}

	ynl_dump_foreach(devs, d) {
		EXPECT_TRUE((bool)d->_len.bus_name);
		EXPECT_TRUE((bool)d->_len.dev_name);
		ksft_print_msg("%s/%s\n", d->bus_name, d->dev_name);
	}

	devlink_get_list_free(devs);
}

TEST_F(devlink, info)
{
	struct devlink_get_list *devs;

	devs = devlink_get_dump(self->ys);
	ASSERT_NE(NULL, devs) {
		TH_LOG("dump failed: %s", self->ys->err.msg);
	}

	if (ynl_dump_empty(devs)) {
		devlink_get_list_free(devs);
		SKIP(return, "no devices to query");
	}

	ynl_dump_foreach(devs, d) {
		struct devlink_info_get_req *info_req;
		struct devlink_info_get_rsp *info_rsp;
		unsigned int i;

		EXPECT_TRUE((bool)d->_len.bus_name);
		EXPECT_TRUE((bool)d->_len.dev_name);
		ksft_print_msg("%s/%s:\n", d->bus_name, d->dev_name);

		info_req = devlink_info_get_req_alloc();
		ASSERT_NE(NULL, info_req);
		devlink_info_get_req_set_bus_name(info_req, d->bus_name);
		devlink_info_get_req_set_dev_name(info_req, d->dev_name);

		info_rsp = devlink_info_get(self->ys, info_req);
		devlink_info_get_req_free(info_req);
		ASSERT_NE(NULL, info_rsp) {
			devlink_get_list_free(devs);
			TH_LOG("info_get failed: %s", self->ys->err.msg);
		}

		EXPECT_TRUE((bool)info_rsp->_len.info_driver_name);
		if (info_rsp->_len.info_driver_name)
			ksft_print_msg("  driver: %s\n",
				       info_rsp->info_driver_name);
		if (info_rsp->_count.info_version_running)
			ksft_print_msg("  running fw:\n");
		for (i = 0; i < info_rsp->_count.info_version_running; i++)
			ksft_print_msg("    %s: %s\n",
				       info_rsp->info_version_running[i].info_version_name,
				       info_rsp->info_version_running[i].info_version_value);
		devlink_info_get_rsp_free(info_rsp);
	}
	devlink_get_list_free(devs);
}

TEST_HARNESS_MAIN
