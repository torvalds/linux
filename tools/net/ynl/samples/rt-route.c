// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <arpa/inet.h>
#include <net/if.h>

#include "rt-route-user.h"

static void rt_route_print(struct rt_route_getroute_rsp *r)
{
	char ifname[IF_NAMESIZE];
	char route_str[64];
	const char *route;
	const char *name;

	/* Ignore local */
	if (r->_hdr.rtm_table == RT_TABLE_LOCAL)
		return;

	if (r->_present.oif) {
		name = if_indextoname(r->oif, ifname);
		if (name)
			printf("oif: %-16s ", name);
	}

	if (r->_len.dst) {
		route = inet_ntop(r->_hdr.rtm_family, r->dst,
				  route_str, sizeof(route_str));
		printf("dst: %s/%d", route, r->_hdr.rtm_dst_len);
	}

	if (r->_len.gateway) {
		route = inet_ntop(r->_hdr.rtm_family, r->gateway,
				  route_str, sizeof(route_str));
		printf("gateway: %s ", route);
	}

	printf("\n");
}

int main(int argc, char **argv)
{
	struct rt_route_getroute_req_dump *req;
	struct rt_route_getroute_list *rsp;
	struct ynl_error yerr;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_rt_route_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	req = rt_route_getroute_req_dump_alloc();
	if (!req)
		goto err_destroy;

	rsp = rt_route_getroute_dump(ys, req);
	rt_route_getroute_req_dump_free(req);
	if (!rsp)
		goto err_close;

	if (ynl_dump_empty(rsp))
		fprintf(stderr, "Error: no routeesses reported\n");
	ynl_dump_foreach(rsp, route)
		rt_route_print(route);
	rt_route_getroute_list_free(rsp);

	ynl_sock_destroy(ys);
	return 0;

err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
err_destroy:
	ynl_sock_destroy(ys);
	return 2;
}
