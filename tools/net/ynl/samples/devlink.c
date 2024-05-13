// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include "devlink-user.h"

int main(int argc, char **argv)
{
	struct devlink_get_list *devs;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_devlink_family, NULL);
	if (!ys)
		return 1;

	devs = devlink_get_dump(ys);
	if (!devs)
		goto err_close;

	ynl_dump_foreach(devs, d) {
		struct devlink_info_get_req *info_req;
		struct devlink_info_get_rsp *info_rsp;

		printf("%s/%s:\n", d->bus_name, d->dev_name);

		info_req = devlink_info_get_req_alloc();
		devlink_info_get_req_set_bus_name(info_req, d->bus_name);
		devlink_info_get_req_set_dev_name(info_req, d->dev_name);

		info_rsp = devlink_info_get(ys, info_req);
		devlink_info_get_req_free(info_req);
		if (!info_rsp)
			goto err_free_devs;

		if (info_rsp->_present.info_driver_name_len)
			printf("    driver: %s\n", info_rsp->info_driver_name);
		if (info_rsp->n_info_version_running)
			printf("    running fw:\n");
		for (unsigned i = 0; i < info_rsp->n_info_version_running; i++)
			printf("        %s: %s\n",
			       info_rsp->info_version_running[i].info_version_name,
			       info_rsp->info_version_running[i].info_version_value);
		printf("    ...\n");
		devlink_info_get_rsp_free(info_rsp);
	}
	devlink_get_list_free(devs);

	ynl_sock_destroy(ys);

	return 0;

err_free_devs:
	devlink_get_list_free(devs);
err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
	ynl_sock_destroy(ys);
	return 2;
}
