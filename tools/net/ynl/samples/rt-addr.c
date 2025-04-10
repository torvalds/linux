// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <arpa/inet.h>
#include <net/if.h>

#include "rt-addr-user.h"

static void rt_addr_print(struct rt_addr_getaddr_rsp *a)
{
	char ifname[IF_NAMESIZE];
	char addr_str[64];
	const char *addr;
	const char *name;

	name = if_indextoname(a->_hdr.ifa_index, ifname);
	if (name)
		printf("%16s: ", name);

	switch (a->_present.address_len) {
	case 4:
		addr = inet_ntop(AF_INET, a->address,
				 addr_str, sizeof(addr_str));
		break;
	case 16:
		addr = inet_ntop(AF_INET6, a->address,
				 addr_str, sizeof(addr_str));
		break;
	default:
		addr = NULL;
		break;
	}
	if (addr)
		printf("%s", addr);
	else
		printf("[%d]", a->_present.address_len);

	printf("\n");
}

int main(int argc, char **argv)
{
	struct rt_addr_getaddr_list *rsp;
	struct rt_addr_getaddr_req *req;
	struct ynl_error yerr;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_rt_addr_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	req = rt_addr_getaddr_req_alloc();
	if (!req)
		goto err_destroy;

	rsp = rt_addr_getaddr_dump(ys, req);
	rt_addr_getaddr_req_free(req);
	if (!rsp)
		goto err_close;

	if (ynl_dump_empty(rsp))
		fprintf(stderr, "Error: no addresses reported\n");
	ynl_dump_foreach(rsp, addr)
		rt_addr_print(addr);
	rt_addr_getaddr_list_free(rsp);

	ynl_sock_destroy(ys);
	return 0;

err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
err_destroy:
	ynl_sock_destroy(ys);
	return 2;
}
