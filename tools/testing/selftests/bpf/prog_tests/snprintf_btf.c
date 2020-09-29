// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <linux/btf.h>
#include "netif_receive_skb.skel.h"

/* Demonstrate that bpf_snprintf_btf succeeds and that various data types
 * are formatted correctly.
 */
void test_snprintf_btf(void)
{
	struct netif_receive_skb *skel;
	struct netif_receive_skb__bss *bss;
	int err, duration = 0;

	skel = netif_receive_skb__open();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;

	err = netif_receive_skb__load(skel);
	if (CHECK(err, "skel_load", "failed to load skeleton: %d\n", err))
		goto cleanup;

	bss = skel->bss;

	err = netif_receive_skb__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed: %d\n", err))
		goto cleanup;

	/* generate receive event */
	(void) system("ping -c 1 127.0.0.1 > /dev/null");

	if (bss->skip) {
		printf("%s:SKIP:no __builtin_btf_type_id\n", __func__);
		test__skip();
		goto cleanup;
	}

	/*
	 * Make sure netif_receive_skb program was triggered
	 * and it set expected return values from bpf_trace_printk()s
	 * and all tests ran.
	 */
	if (CHECK(bss->ret <= 0,
		  "bpf_snprintf_btf: got return value",
		  "ret <= 0 %ld test %d\n", bss->ret, bss->ran_subtests))
		goto cleanup;

	if (CHECK(bss->ran_subtests == 0, "check if subtests ran",
		  "no subtests ran, did BPF program run?"))
		goto cleanup;

	if (CHECK(bss->num_subtests != bss->ran_subtests,
		  "check all subtests ran",
		  "only ran %d of %d tests\n", bss->num_subtests,
		  bss->ran_subtests))
		goto cleanup;

cleanup:
	netif_receive_skb__destroy(skel);
}
