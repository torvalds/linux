// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>

#include <ynl.h>

#include <net/if.h>

#include "tc-user.h"

static void tc_qdisc_print(struct tc_getqdisc_rsp *q)
{
	char ifname[IF_NAMESIZE];
	const char *name;

	name = if_indextoname(q->_hdr.tcm_ifindex, ifname);
	if (name)
		printf("%16s: ", name);

	if (q->_len.kind) {
		printf("%s  ", q->kind);

		if (q->options._present.fq_codel) {
			struct tc_fq_codel_attrs *fq_codel;
			struct tc_fq_codel_xstats *stats;

			fq_codel = &q->options.fq_codel;
			stats = q->stats2.app.fq_codel;

			if (fq_codel->_present.limit)
				printf("limit: %dp ", fq_codel->limit);
			if (fq_codel->_present.target)
				printf("target: %dms ",
				       (fq_codel->target + 500) / 1000);
			if (q->stats2.app._len.fq_codel)
				printf("new_flow_cnt: %d ",
				       stats->qdisc_stats.new_flow_count);
		}
	}

	printf("\n");
}

int main(int argc, char **argv)
{
	struct tc_getqdisc_req_dump *req;
	struct tc_getqdisc_list *rsp;
	struct ynl_error yerr;
	struct ynl_sock *ys;

	ys = ynl_sock_create(&ynl_tc_family, &yerr);
	if (!ys) {
		fprintf(stderr, "YNL: %s\n", yerr.msg);
		return 1;
	}

	req = tc_getqdisc_req_dump_alloc();
	if (!req)
		goto err_destroy;

	rsp = tc_getqdisc_dump(ys, req);
	tc_getqdisc_req_dump_free(req);
	if (!rsp)
		goto err_close;

	if (ynl_dump_empty(rsp))
		fprintf(stderr, "Error: no addresses reported\n");
	ynl_dump_foreach(rsp, qdisc)
		tc_qdisc_print(qdisc);
	tc_getqdisc_list_free(rsp);

	ynl_sock_destroy(ys);
	return 0;

err_close:
	fprintf(stderr, "YNL: %s\n", ys->err.msg);
err_destroy:
	ynl_sock_destroy(ys);
	return 2;
}
