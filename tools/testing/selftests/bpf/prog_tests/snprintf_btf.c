// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <linux/btf.h>
#include <bpf/btf.h>
#include "netif_receive_skb.skel.h"
#include "snprintf_btf_void.skel.h"

/* Demonstrate that bpf_snprintf_btf succeeds and that various data types
 * are formatted correctly.
 */
void serial_test_snprintf_btf(void)
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
	err = system("ping -c 1 127.0.0.1 > /dev/null");
	if (CHECK(err, "system", "ping failed: %d\n", err))
		goto cleanup;

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
	if (!ASSERT_GT(bss->ret, 0, "bpf_snprintf_ret"))
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

/*
 * bpf_snprintf_btf() renders a type_id taken straight from the vmlinux BTF.
 * Two such type_ids used to NULL-deref in the BTF show path:
 *   - a "const void" (a modifier resolving to void) in btf_modifier_show()
 *   - a BTF_KIND_VAR in btf_var_show() (base BTF has no resolved_ids)
 * A fixed kernel renders both without crashing.
 */
static long run(struct snprintf_btf_void *skel, __u32 type_id)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	char ctx[8] = {};

	skel->bss->type_id = type_id;
	topts.ctx_in = ctx;
	topts.ctx_size_in = sizeof(ctx);
	if (!ASSERT_OK(bpf_prog_test_run_opts(bpf_program__fd(skel->progs.dump_type),
					      &topts), "test_run"))
		return -1;
	return skel->bss->ret;
}

void test_snprintf_btf_void(void)
{
	const struct btf_type *t;
	struct snprintf_btf_void *skel;
	int i, n, cv = 0, var = 0;
	struct btf *btf;

	btf = btf__parse("/sys/kernel/btf/vmlinux", NULL);
	if (!btf) {
		test__skip();
		return;
	}

	skel = snprintf_btf_void__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		goto out_btf;

	n = btf__type_cnt(btf);
	for (i = 1; i < n && !(cv && var); i++) {
		t = btf__type_by_id(btf, i);
		if (!cv && btf_kind(t) == BTF_KIND_CONST && t->type == 0)
			cv = i;
		/* Pick a VAR small enough to render from the program's buffer. */
		if (!var && btf_kind(t) == BTF_KIND_VAR) {
			long sz = btf__resolve_size(btf, t->type);

			if (sz > 0 && sz <= (long)sizeof(skel->bss->obj))
				var = i;
		}
	}

	/* "const void" renders the "<unsupported kind:0>" placeholder. */
	if (test__start_subtest("const_void")) {
		if (cv) {
			ASSERT_EQ(run(skel, cv),
				  sizeof("<unsupported kind:0>") - 1, "ret");
			ASSERT_STREQ(skel->bss->out, "<unsupported kind:0>",
				     "placeholder");
		} else {
			test__skip();
		}
	}

	/* A BTF_KIND_VAR must resolve and render without error. */
	if (test__start_subtest("var")) {
		if (var)
			ASSERT_GT(run(skel, var), 0, "ret");
		else
			test__skip();
	}

	snprintf_btf_void__destroy(skel);
out_btf:
	btf__free(btf);
}
