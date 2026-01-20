// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <linux/pkt_sched.h>
#include <linux/tc_act/tc_vlan.h>
#include <linux/tc_act/tc_gact.h>
#include <linux/if_ether.h>
#include <net/if.h>

#include <ynl.h>

#include "tc-user.h"

#define TC_HANDLE (0xFFFF << 16)

const char *vlan_act_name(struct tc_vlan *p)
{
	switch (p->v_action) {
	case TCA_VLAN_ACT_POP:
		return "pop";
	case TCA_VLAN_ACT_PUSH:
		return "push";
	case TCA_VLAN_ACT_MODIFY:
		return "modify";
	default:
		break;
	}

	return "not supported";
}

const char *gact_act_name(struct tc_gact *p)
{
	switch (p->action) {
	case TC_ACT_SHOT:
		return "drop";
	case TC_ACT_OK:
		return "ok";
	case TC_ACT_PIPE:
		return "pipe";
	default:
		break;
	}

	return "not supported";
}

static void print_vlan(struct tc_act_vlan_attrs *vlan)
{
	printf("%s ", vlan_act_name(vlan->parms));
	if (vlan->_present.push_vlan_id)
		printf("id %u ", vlan->push_vlan_id);
	if (vlan->_present.push_vlan_protocol)
		printf("protocol %#x ", ntohs(vlan->push_vlan_protocol));
	if (vlan->_present.push_vlan_priority)
		printf("priority %u ", vlan->push_vlan_priority);
}

static void print_gact(struct tc_act_gact_attrs *gact)
{
	struct tc_gact *p = gact->parms;

	printf("%s ", gact_act_name(p));
}

static void flower_print(struct tc_flower_attrs *flower, const char *kind)
{
	struct tc_act_attrs *a;
	unsigned int i;

	printf("%s:\n", kind);

	if (flower->_present.key_vlan_id)
		printf("  vlan_id: %u\n", flower->key_vlan_id);
	if (flower->_present.key_vlan_prio)
		printf("  vlan_prio: %u\n", flower->key_vlan_prio);
	if (flower->_present.key_num_of_vlans)
		printf("  num_of_vlans: %u\n", flower->key_num_of_vlans);

	for (i = 0; i < flower->_count.act; i++) {
		a = &flower->act[i];
		printf("action order: %i %s ", i + 1, a->kind);
		if (a->options._present.vlan)
			print_vlan(&a->options.vlan);
		else if (a->options._present.gact)
			print_gact(&a->options.gact);
		printf("\n");
	}
	printf("\n");
}

static void tc_filter_print(struct tc_gettfilter_rsp *f)
{
	struct tc_options_msg *opt = &f->options;

	if (opt->_present.flower)
		flower_print(&opt->flower, f->kind);
	else if (f->_len.kind)
		printf("%s pref %u proto: %#x\n", f->kind,
		       (f->_hdr.tcm_info >> 16),
			ntohs(TC_H_MIN(f->_hdr.tcm_info)));
}

static int tc_filter_add(struct ynl_sock *ys, int ifi)
{
	struct tc_newtfilter_req *req;
	struct tc_act_attrs *acts;
	struct tc_vlan p = {
		.action = TC_ACT_PIPE,
		.v_action = TCA_VLAN_ACT_PUSH
	};
	__u16 flags = NLM_F_REQUEST | NLM_F_EXCL | NLM_F_CREATE;
	int ret;

	req = tc_newtfilter_req_alloc();
	if (!req) {
		fprintf(stderr, "tc_newtfilter_req_alloc failed\n");
		return -1;
	}
	memset(req, 0, sizeof(*req));

	acts = tc_act_attrs_alloc(3);
	if (!acts) {
		fprintf(stderr, "tc_act_attrs_alloc\n");
		tc_newtfilter_req_free(req);
		return -1;
	}
	memset(acts, 0, sizeof(*acts) * 3);

	req->_hdr.tcm_ifindex = ifi;
	req->_hdr.tcm_parent = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	req->_hdr.tcm_info = TC_H_MAKE(1 << 16, htons(ETH_P_8021Q));
	req->chain = 0;

	tc_newtfilter_req_set_nlflags(req, flags);
	tc_newtfilter_req_set_kind(req, "flower");
	tc_newtfilter_req_set_options_flower_key_vlan_id(req, 100);
	tc_newtfilter_req_set_options_flower_key_vlan_prio(req, 5);
	tc_newtfilter_req_set_options_flower_key_num_of_vlans(req, 3);

	__tc_newtfilter_req_set_options_flower_act(req, acts, 3);

	/* Skip action at index 0 because in TC, the action array
	 * index starts at 1, with each index defining the action's
	 * order. In contrast, in YNL indexed arrays start at index 0.
	 */
	tc_act_attrs_set_kind(&acts[1], "vlan");
	tc_act_attrs_set_options_vlan_parms(&acts[1], &p, sizeof(p));
	tc_act_attrs_set_options_vlan_push_vlan_id(&acts[1], 200);
	tc_act_attrs_set_kind(&acts[2], "vlan");
	tc_act_attrs_set_options_vlan_parms(&acts[2], &p, sizeof(p));
	tc_act_attrs_set_options_vlan_push_vlan_id(&acts[2], 300);

	tc_newtfilter_req_set_options_flower_flags(req, 0);
	tc_newtfilter_req_set_options_flower_key_eth_type(req, htons(0x8100));

	ret = tc_newtfilter(ys, req);
	if (ret)
		fprintf(stderr, "tc_newtfilter: %s\n", ys->err.msg);

	tc_newtfilter_req_free(req);

	return ret;
}

static int tc_filter_show(struct ynl_sock *ys, int ifi)
{
	struct tc_gettfilter_req_dump *req;
	struct tc_gettfilter_list *rsp;

	req = tc_gettfilter_req_dump_alloc();
	if (!req) {
		fprintf(stderr, "tc_gettfilter_req_dump_alloc failed\n");
		return -1;
	}
	memset(req, 0, sizeof(*req));

	req->_hdr.tcm_ifindex = ifi;
	req->_hdr.tcm_parent = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	req->_present.chain = 1;
	req->chain = 0;

	rsp = tc_gettfilter_dump(ys, req);
	tc_gettfilter_req_dump_free(req);
	if (!rsp) {
		fprintf(stderr, "YNL: %s\n", ys->err.msg);
		return -1;
	}

	if (ynl_dump_empty(rsp))
		fprintf(stderr, "Error: no filters reported\n");
	else
		ynl_dump_foreach(rsp, flt) tc_filter_print(flt);

	tc_gettfilter_list_free(rsp);

	return 0;
}

static int tc_filter_del(struct ynl_sock *ys, int ifi)
{
	struct tc_deltfilter_req *req;
	__u16 flags = NLM_F_REQUEST;
	int ret;

	req = tc_deltfilter_req_alloc();
	if (!req) {
		fprintf(stderr, "tc_deltfilter_req_alloc failed\n");
		return -1;
	}
	memset(req, 0, sizeof(*req));

	req->_hdr.tcm_ifindex = ifi;
	req->_hdr.tcm_parent = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	req->_hdr.tcm_info = TC_H_MAKE(1 << 16, htons(ETH_P_8021Q));
	tc_deltfilter_req_set_nlflags(req, flags);

	ret = tc_deltfilter(ys, req);
	if (ret)
		fprintf(stderr, "tc_deltfilter failed: %s\n", ys->err.msg);

	tc_deltfilter_req_free(req);

	return ret;
}

static int tc_clsact_add(struct ynl_sock *ys, int ifi)
{
	struct tc_newqdisc_req *req;
	__u16 flags = NLM_F_REQUEST | NLM_F_EXCL | NLM_F_CREATE;
	int ret;

	req = tc_newqdisc_req_alloc();
	if (!req) {
		fprintf(stderr, "tc_newqdisc_req_alloc failed\n");
		return -1;
	}
	memset(req, 0, sizeof(*req));

	req->_hdr.tcm_ifindex = ifi;
	req->_hdr.tcm_parent = TC_H_CLSACT;
	req->_hdr.tcm_handle = TC_HANDLE;
	tc_newqdisc_req_set_nlflags(req, flags);
	tc_newqdisc_req_set_kind(req, "clsact");

	ret = tc_newqdisc(ys, req);
	if (ret)
		fprintf(stderr, "tc_newqdisc failed: %s\n", ys->err.msg);

	tc_newqdisc_req_free(req);

	return ret;
}

static int tc_clsact_del(struct ynl_sock *ys, int ifi)
{
	struct tc_delqdisc_req *req;
	__u16 flags = NLM_F_REQUEST;
	int ret;

	req = tc_delqdisc_req_alloc();
	if (!req) {
		fprintf(stderr, "tc_delqdisc_req_alloc failed\n");
		return -1;
	}
	memset(req, 0, sizeof(*req));

	req->_hdr.tcm_ifindex = ifi;
	req->_hdr.tcm_parent = TC_H_CLSACT;
	req->_hdr.tcm_handle = TC_HANDLE;
	tc_delqdisc_req_set_nlflags(req, flags);

	ret = tc_delqdisc(ys, req);
	if (ret)
		fprintf(stderr, "tc_delqdisc failed: %s\n", ys->err.msg);

	tc_delqdisc_req_free(req);

	return ret;
}

static int tc_filter_config(struct ynl_sock *ys, int ifi)
{
	int ret = 0;

	if (tc_filter_add(ys, ifi))
		return -1;

	ret = tc_filter_show(ys, ifi);

	if (tc_filter_del(ys, ifi))
		return -1;

	return ret;
}

int main(int argc, char **argv)
{
	struct ynl_error yerr;
	struct ynl_sock *ys;
	int ifi, ret = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s <interface_name>\n", argv[0]);
		return 1;
	}
	ifi = if_nametoindex(argv[1]);
	if (!ifi) {
		perror("if_nametoindex");
		return 1;
	}

	ys = ynl_sock_create(&ynl_tc_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	if (tc_clsact_add(ys, ifi)) {
		ret = 2;
		goto err_destroy;
	}

	if (tc_filter_config(ys, ifi))
		ret = 3;

	if (tc_clsact_del(ys, ifi))
		ret = 4;

err_destroy:
	ynl_sock_destroy(ys);
	return ret;
}
