// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <arpa/inet.h>
#include <net/if.h>

#include "rt-link-user.h"

static void rt_link_print(struct rt_link_getlink_rsp *r)
{
	unsigned int i;

	printf("%3d: ", r->_hdr.ifi_index);

	if (r->_len.ifname)
		printf("%16s: ", r->ifname);

	if (r->_present.mtu)
		printf("mtu %5d  ", r->mtu);

	if (r->linkinfo._len.kind)
		printf("kind %-8s  ", r->linkinfo.kind);
	else
		printf("     %8s  ", "");

	if (r->prop_list._count.alt_ifname) {
		printf("altname ");
		for (i = 0; i < r->prop_list._count.alt_ifname; i++)
			printf("%s ", r->prop_list.alt_ifname[i]->str);
		printf(" ");
	}

	if (r->linkinfo._present.data && r->linkinfo.data._present.netkit) {
		struct rt_link_linkinfo_netkit_attrs *netkit;
		const char *name;

		netkit = &r->linkinfo.data.netkit;
		printf("primary %d  ", netkit->primary);

		name = NULL;
		if (netkit->_present.policy)
			name = rt_link_netkit_policy_str(netkit->policy);
		if (name)
			printf("policy %s  ", name);
	}

	printf("\n");
}

static int rt_link_create_netkit(struct ynl_sock *ys)
{
	struct rt_link_getlink_ntf *ntf_gl;
	struct rt_link_newlink_req *req;
	struct ynl_ntf_base_type *ntf;
	int ret;

	req = rt_link_newlink_req_alloc();
	if (!req) {
		fprintf(stderr, "Can't alloc req\n");
		return -1;
	}

	/* rtnetlink doesn't provide info about the created object.
	 * It expects us to set the ECHO flag and the dig the info out
	 * of the notifications...
	 */
	rt_link_newlink_req_set_nlflags(req, NLM_F_CREATE | NLM_F_ECHO);

	rt_link_newlink_req_set_linkinfo_kind(req, "netkit");

	/* Test error messages */
	rt_link_newlink_req_set_linkinfo_data_netkit_policy(req, 10);
	ret = rt_link_newlink(ys, req);
	if (ret) {
		printf("Testing error message for policy being bad:\n\t%s\n", ys->err.msg);
	} else {
		fprintf(stderr,	"Warning: unexpected success creating netkit with bad attrs\n");
		goto created;
	}

	rt_link_newlink_req_set_linkinfo_data_netkit_policy(req, NETKIT_DROP);

	ret = rt_link_newlink(ys, req);
created:
	rt_link_newlink_req_free(req);
	if (ret) {
		fprintf(stderr, "YNL: %s\n", ys->err.msg);
		return -1;
	}

	if (!ynl_has_ntf(ys)) {
		fprintf(stderr,
			"Warning: interface created but received no notification, won't delete the interface\n");
		return 0;
	}

	ntf = ynl_ntf_dequeue(ys);
	if (ntf->cmd !=	RTM_NEWLINK) {
		fprintf(stderr,
			"Warning: unexpected notification type, won't delete the interface\n");
		return 0;
	}
	ntf_gl = (void *)ntf;
	ret = ntf_gl->obj._hdr.ifi_index;
	ynl_ntf_free(ntf);

	return ret;
}

static void rt_link_del(struct ynl_sock *ys, int ifindex)
{
	struct rt_link_dellink_req *req;

	req = rt_link_dellink_req_alloc();
	if (!req) {
		fprintf(stderr, "Can't alloc req\n");
		return;
	}

	req->_hdr.ifi_index = ifindex;
	if (rt_link_dellink(ys, req))
		fprintf(stderr, "YNL: %s\n", ys->err.msg);
	else
		fprintf(stderr,
			"Trying to delete a Netkit interface (ifindex %d)\n",
			ifindex);

	rt_link_dellink_req_free(req);
}

int main(int argc, char **argv)
{
	struct rt_link_getlink_req_dump *req;
	struct rt_link_getlink_list *rsp;
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int created = 0;

	ys = ynl_sock_create(&ynl_rt_link_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	if (argc > 1) {
		fprintf(stderr, "Trying to create a Netkit interface\n");
		created = rt_link_create_netkit(ys);
		if (created < 0)
			goto err_destroy;
	}

	req = rt_link_getlink_req_dump_alloc();
	if (!req)
		goto err_del_ifc;

	rsp = rt_link_getlink_dump(ys, req);
	rt_link_getlink_req_dump_free(req);
	if (!rsp)
		goto err_close;

	if (ynl_dump_empty(rsp))
		fprintf(stderr, "Error: no links reported\n");
	ynl_dump_foreach(rsp, link)
		rt_link_print(link);
	rt_link_getlink_list_free(rsp);

	if (created)
		rt_link_del(ys, created);

	ynl_sock_destroy(ys);
	return 0;

err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
err_del_ifc:
	if (created)
		rt_link_del(ys, created);
err_destroy:
	ynl_sock_destroy(ys);
	return 2;
}
