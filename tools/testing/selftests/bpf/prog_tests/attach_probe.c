// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "test_attach_probe.skel.h"

ssize_t get_base_addr() {
	size_t start, offset;
	char buf[256];
	FILE *f;

	f = fopen("/proc/self/maps", "r");
	if (!f)
		return -errno;

	while (fscanf(f, "%zx-%*x %s %zx %*[^\n]\n",
		      &start, buf, &offset) == 3) {
		if (strcmp(buf, "r-xp") == 0) {
			fclose(f);
			return start - offset;
		}
	}

	fclose(f);
	return -EINVAL;
}

void test_attach_probe(void)
{
	int duration = 0;
	struct bpf_link *kprobe_link, *kretprobe_link;
	struct bpf_link *uprobe_link, *uretprobe_link;
	struct test_attach_probe* skel;
	size_t uprobe_offset;
	ssize_t base_addr;

	base_addr = get_base_addr();
	if (CHECK(base_addr < 0, "get_base_addr",
		  "failed to find base addr: %zd", base_addr))
		return;
	uprobe_offset = (size_t)&get_base_addr - base_addr;

	skel = test_attach_probe__open_and_load();
	if (CHECK(!skel, "skel_open", "failed to open skeleton\n"))
		return;
	if (CHECK(!skel->bss, "check_bss", ".bss wasn't mmap()-ed\n"))
		goto cleanup;

	kprobe_link = bpf_program__attach_kprobe(skel->progs.handle_kprobe,
						 false /* retprobe */,
						 SYS_NANOSLEEP_KPROBE_NAME);
	if (CHECK(IS_ERR(kprobe_link), "attach_kprobe",
		  "err %ld\n", PTR_ERR(kprobe_link)))
		goto cleanup;
	skel->links.handle_kprobe = kprobe_link;

	kretprobe_link = bpf_program__attach_kprobe(skel->progs.handle_kretprobe,
						    true /* retprobe */,
						    SYS_NANOSLEEP_KPROBE_NAME);
	if (CHECK(IS_ERR(kretprobe_link), "attach_kretprobe",
		  "err %ld\n", PTR_ERR(kretprobe_link)))
		goto cleanup;
	skel->links.handle_kretprobe = kretprobe_link;

	uprobe_link = bpf_program__attach_uprobe(skel->progs.handle_uprobe,
						 false /* retprobe */,
						 0 /* self pid */,
						 "/proc/self/exe",
						 uprobe_offset);
	if (CHECK(IS_ERR(uprobe_link), "attach_uprobe",
		  "err %ld\n", PTR_ERR(uprobe_link)))
		goto cleanup;
	skel->links.handle_uprobe = uprobe_link;

	uretprobe_link = bpf_program__attach_uprobe(skel->progs.handle_uretprobe,
						    true /* retprobe */,
						    -1 /* any pid */,
						    "/proc/self/exe",
						    uprobe_offset);
	if (CHECK(IS_ERR(uretprobe_link), "attach_uretprobe",
		  "err %ld\n", PTR_ERR(uretprobe_link)))
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
	get_base_addr();

	if (CHECK(skel->bss->uprobe_res != 3, "check_uprobe_res",
		  "wrong uprobe res: %d\n", skel->bss->uprobe_res))
		goto cleanup;
	if (CHECK(skel->bss->uretprobe_res != 4, "check_uretprobe_res",
		  "wrong uretprobe res: %d\n", skel->bss->uretprobe_res))
		goto cleanup;

cleanup:
	test_attach_probe__destroy(skel);
}
