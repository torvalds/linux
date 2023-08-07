// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <net/if.h>

#include "netdev-user.h"

/* netdev genetlink family code sample
 * This sample shows off basics of the netdev family but also notification
 * handling, hence the somewhat odd UI. We subscribe to notifications first
 * then wait for ifc selection, so the socket may already accumulate
 * notifications as we wait. This allows us to test that YNL can handle
 * requests and notifications getting interleaved.
 */

static void netdev_print_device(struct netdev_dev_get_rsp *d, unsigned int op)
{
	char ifname[IF_NAMESIZE];
	const char *name;

	if (!d->_present.ifindex)
		return;

	name = if_indextoname(d->ifindex, ifname);
	if (name)
		printf("%8s", name);
	printf("[%d]\t", d->ifindex);

	if (!d->_present.xdp_features)
		return;

	printf("%llx:", d->xdp_features);
	for (int i = 0; d->xdp_features > 1U << i; i++) {
		if (d->xdp_features & (1U << i))
			printf(" %s", netdev_xdp_act_str(1 << i));
	}

	name = netdev_op_str(op);
	if (name)
		printf(" (ntf: %s)", name);
	printf("\n");
}

int main(int argc, char **argv)
{
	struct netdev_dev_get_list *devs;
	struct ynl_ntf_base_type *ntf;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ifindex = 0;

	if (argc > 1)
		ifindex = strtol(argv[1], NULL, 0);

	ys = ynl_sock_create(&ynl_netdev_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	if (ynl_subscribe(ys, "mgmt"))
		goto err_close;

	printf("Select ifc ($ifindex; or 0 = dump; or -2 ntf check): ");
	scanf("%d", &ifindex);

	if (ifindex > 0) {
		struct netdev_dev_get_req *req;
		struct netdev_dev_get_rsp *d;

		req = netdev_dev_get_req_alloc();
		netdev_dev_get_req_set_ifindex(req, ifindex);

		d = netdev_dev_get(ys, req);
		netdev_dev_get_req_free(req);
		if (!d)
			goto err_close;

		netdev_print_device(d, 0);
		netdev_dev_get_rsp_free(d);
	} else if (!ifindex) {
		devs = netdev_dev_get_dump(ys);
		if (!devs)
			goto err_close;

		ynl_dump_foreach(devs, d)
			netdev_print_device(d, 0);
		netdev_dev_get_list_free(devs);
	} else if (ifindex == -2) {
		ynl_ntf_check(ys);
	}
	while ((ntf = ynl_ntf_dequeue(ys))) {
		netdev_print_device((struct netdev_dev_get_rsp *)&ntf->data,
				    ntf->cmd);
		ynl_ntf_free(ntf);
	}

	ynl_sock_destroy(ys);
	return 0;

err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
	ynl_sock_destroy(ys);
	return 2;
}
