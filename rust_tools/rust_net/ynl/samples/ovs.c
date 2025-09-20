// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include "ovs_datapath-user.h"

int main(int argc, char **argv)
{
	struct ynl_sock *ys;
	int err;

	ys = ynl_sock_create(&ynl_ovs_datapath_family, NULL);
	if (!ys)
		return 1;

	if (argc > 1) {
		struct ovs_datapath_new_req *req;

		req = ovs_datapath_new_req_alloc();
		if (!req)
			goto err_close;

		ovs_datapath_new_req_set_upcall_pid(req, 1);
		ovs_datapath_new_req_set_name(req, argv[1]);

		err = ovs_datapath_new(ys, req);
		ovs_datapath_new_req_free(req);
		if (err)
			goto err_close;
	} else {
		struct ovs_datapath_get_req_dump *req;
		struct ovs_datapath_get_list *dps;

		printf("Dump:\n");
		req = ovs_datapath_get_req_dump_alloc();

		dps = ovs_datapath_get_dump(ys, req);
		ovs_datapath_get_req_dump_free(req);
		if (!dps)
			goto err_close;

		ynl_dump_foreach(dps, dp) {
			printf("  %s(%d): pid:%u cache:%u\n",
			       dp->name, dp->_hdr.dp_ifindex,
			       dp->upcall_pid, dp->masks_cache_size);
		}
		ovs_datapath_get_list_free(dps);
	}

	ynl_sock_destroy(ys);

	return 0;

err_close:
	fprintf(stderr, "YNL (%d): %s\n", ys->err.code, ys->err.msg);
	ynl_sock_destroy(ys);
	return 2;
}
