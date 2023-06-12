// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <net/if.h>

#include "ethtool-user.h"

int main(int argc, char **argv)
{
	struct ethtool_channels_get_req_dump creq = {};
	struct ethtool_rings_get_req_dump rreq = {};
	struct ethtool_channels_get_list *channels;
	struct ethtool_rings_get_list *rings;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_ethtool_family, NULL);
	if (!ys)
		return 1;

	creq._present.header = 1; /* ethtool needs an empty nest, sigh */
	channels = ethtool_channels_get_dump(ys, &creq);
	if (!channels)
		goto err_close;

	printf("Channels:\n");
	ynl_dump_foreach(channels, dev) {
		printf("  %8s: ", dev->header.dev_name);
		if (dev->_present.rx_count)
			printf("rx %d ", dev->rx_count);
		if (dev->_present.tx_count)
			printf("tx %d ", dev->tx_count);
		if (dev->_present.combined_count)
			printf("combined %d ", dev->combined_count);
		printf("\n");
	}
	ethtool_channels_get_list_free(channels);

	rreq._present.header = 1; /* ethtool needs an empty nest.. */
	rings = ethtool_rings_get_dump(ys, &rreq);
	if (!rings)
		goto err_close;

	printf("Rings:\n");
	ynl_dump_foreach(rings, dev) {
		printf("  %8s: ", dev->header.dev_name);
		if (dev->_present.rx)
			printf("rx %d ", dev->rx);
		if (dev->_present.tx)
			printf("tx %d ", dev->tx);
		printf("\n");
	}
	ethtool_rings_get_list_free(rings);

	ynl_sock_destroy(ys);

	return 0;

err_close:
	fprintf(stderr, "YNL (%d): %s\n", ys->err.code, ys->err.msg);
	ynl_sock_destroy(ys);
	return 2;
}
