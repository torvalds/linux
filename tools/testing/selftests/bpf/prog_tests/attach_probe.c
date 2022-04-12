// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_attach_probe.skel.h"

/* this is how USDT semaphore is actually defined, except volatile modifier */
volatile unsigned short uprobe_ref_ctr __attribute__((unused)) __attribute((section(".probes")));

/* uprobe attach point */
static void trigger_func(void)
{
	asm volatile ("");
}

void test_attach_probe(void)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, uprobe_opts);
	int duration = 0;
	struct bpf_link *kprobe_link, *kretprobe_link;
	struct bpf_link *uprobe_link, *uretprobe_link;
	struct test_attach_probe* skel;
	ssize_t uprobe_offset, ref_ctr_offset;
	bool legacy;

	/* Check if new-style kprobe/uprobe API is supported.
	 * Kernels that support new FD-based kprobe and uprobe BPF attachment
	 * through perf_event_open() syscall expose
	 * /sys/bus/event_source/devices/kprobe/type and
	 * /sys/bus/event_source/devices/uprobe/type files, respectively. They
	 * contain magic numbers that are passed as "type" field of
	 * perf_event_attr. Lack of such file in the system indicates legacy
	 * kernel with old-style kprobe/uprobe attach interface through
	 * creating per-probe event through tracefs. For such cases
	 * ref_ctr_offset feature is not supported, so we don't test it.
	 */
	legacy = access("/sys/bus/event_source/devices/kprobe/type", F_OK) != 0;

	uprobe_offset = get_uprobe_offset(&trigger_func);
	if (!ASSERT_GE(uprobe_offset, 0, "uprobe_offset"))
		return;

	ref_ctr_offset = get_rel_offset((uintptr_t)&uprobe_ref_ctr);
	if (!ASSERT_GE(ref_ctr_offset, 0, "ref_ctr_offset"))
		return;

	skel = test_attach_probe__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;
	if (CHECK(!skel->bss, "check_bss", ".bss wasn't mmap()-ed\n"))
		goto cleanup;

	kprobe_link = bpf_program__attach_kprobe(skel->progs.handle_kprobe,
						 false /* retprobe */,
						 SYS_NANOSLEEP_KPROBE_NAME);
	if (!ASSERT_OK_PTR(kprobe_link, "attach_kprobe"))
		goto cleanup;
	skel->links.handle_kprobe = kprobe_link;

	kretprobe_link = bpf_program__attach_kprobe(skel->progs.handle_kretprobe,
						    true /* retprobe */,
						    SYS_NANOSLEEP_KPROBE_NAME);
	if (!ASSERT_OK_PTR(kretprobe_link, "attach_kretprobe"))
		goto cleanup;
	skel->links.handle_kretprobe = kretprobe_link;

	if (!legacy)
		ASSERT_EQ(uprobe_ref_ctr, 0, "uprobe_ref_ctr_before");

	uprobe_opts.retprobe = false;
	uprobe_opts.ref_ctr_offset = legacy ? 0 : ref_ctr_offset;
	uprobe_link = bpf_program__attach_uprobe_opts(skel->progs.handle_uprobe,
						      0 /* self pid */,
						      "/proc/self/exe",
						      uprobe_offset,
						      &uprobe_opts);
	if (!ASSERT_OK_PTR(uprobe_link, "attach_uprobe"))
		goto cleanup;
	skel->links.handle_uprobe = uprobe_link;

	if (!legacy)
		ASSERT_GT(uprobe_ref_ctr, 0, "uprobe_ref_ctr_after");

	/* if uprobe uses ref_ctr, uretprobe has to use ref_ctr as well */
	uprobe_opts.retprobe = true;
	uprobe_opts.ref_ctr_offset = legacy ? 0 : ref_ctr_offset;
	uretprobe_link = bpf_program__attach_uprobe_opts(skel->progs.handle_uretprobe,
							 -1 /* any pid */,
							 "/proc/self/exe",
							 uprobe_offset, &uprobe_opts);
	if (!ASSERT_OK_PTR(uretprobe_link, "attach_uretprobe"))
		goto cleanup;
	skel->links.handle_uretprobe = uretprobe_link;

	/* trigger & validate kprobe && kretprobe */
	usleep(1);

	if (CHECK(skel->bss->kprobe_res != 1, "check_kprobe_res",
		  "wrong kprobe res: %d\n", skel->bss->kprobe_res))
		goto cleanup;
	if (CHECK(skel->bss->kretprobe_res != 2, "check_kretprobe_res",
		  "wrong kretprobe res: %d\n", skel->bss->kretprobe_res))
		goto cleanup;

	/* trigger & validate uprobe & uretprobe */
	trigger_func();

	if (CHECK(skel->bss->uprobe_res != 3, "check_uprobe_res",
		  "wrong uprobe res: %d\n", skel->bss->uprobe_res))
		goto cleanup;
	if (CHECK(skel->bss->uretprobe_res != 4, "check_uretprobe_res",
		  "wrong uretprobe res: %d\n", skel->bss->uretprobe_res))
		goto cleanup;

cleanup:
	test_attach_probe__destroy(skel);
	ASSERT_EQ(uprobe_ref_ctr, 0, "uprobe_ref_ctr_cleanup");
}
