// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include <string.h>
#include <linux/bpf.h>
#include <linux/limits.h>
#include <test_progs.h>
#include "trace_helpers.h"
#include "test_fill_link_info.skel.h"

#define TP_CAT "sched"
#define TP_NAME "sched_switch"

static const char *kmulti_syms[] = {
	"bpf_fentry_test2",
	"bpf_fentry_test1",
	"bpf_fentry_test3",
};
#define KMULTI_CNT ARRAY_SIZE(kmulti_syms)
static __u64 kmulti_addrs[KMULTI_CNT];

#define KPROBE_FUNC "bpf_fentry_test1"
static __u64 kprobe_addr;

#define UPROBE_FILE "/proc/self/exe"
static ssize_t uprobe_offset;
/* uprobe attach point */
static noinline void uprobe_func(void)
{
	asm volatile ("");
}

static int verify_perf_link_info(int fd, enum bpf_perf_event_type type, long addr,
				 ssize_t offset, ssize_t entry_offset)
{
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	char buf[PATH_MAX];
	int err;

	memset(&info, 0, sizeof(info));
	buf[0] = '\0';

again:
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	if (!ASSERT_OK(err, "get_link_info"))
		return -1;

	if (!ASSERT_EQ(info.type, BPF_LINK_TYPE_PERF_EVENT, "link_type"))
		return -1;
	if (!ASSERT_EQ(info.perf_event.type, type, "perf_type_match"))
		return -1;

	switch (info.perf_event.type) {
	case BPF_PERF_EVENT_KPROBE:
	case BPF_PERF_EVENT_KRETPROBE:
		ASSERT_EQ(info.perf_event.kprobe.offset, offset, "kprobe_offset");

		/* In case kernel.kptr_restrict is not permitted or MAX_SYMS is reached */
		if (addr)
			ASSERT_EQ(info.perf_event.kprobe.addr, addr + entry_offset,
				  "kprobe_addr");

		if (!info.perf_event.kprobe.func_name) {
			ASSERT_EQ(info.perf_event.kprobe.name_len, 0, "name_len");
			info.perf_event.kprobe.func_name = ptr_to_u64(&buf);
			info.perf_event.kprobe.name_len = sizeof(buf);
			goto again;
		}

		err = strncmp(u64_to_ptr(info.perf_event.kprobe.func_name), KPROBE_FUNC,
			      strlen(KPROBE_FUNC));
		ASSERT_EQ(err, 0, "cmp_kprobe_func_name");
		break;
	case BPF_PERF_EVENT_TRACEPOINT:
		if (!info.perf_event.tracepoint.tp_name) {
			ASSERT_EQ(info.perf_event.tracepoint.name_len, 0, "name_len");
			info.perf_event.tracepoint.tp_name = ptr_to_u64(&buf);
			info.perf_event.tracepoint.name_len = sizeof(buf);
			goto again;
		}

		err = strncmp(u64_to_ptr(info.perf_event.tracepoint.tp_name), TP_NAME,
			      strlen(TP_NAME));
		ASSERT_EQ(err, 0, "cmp_tp_name");
		break;
	case BPF_PERF_EVENT_UPROBE:
	case BPF_PERF_EVENT_URETPROBE:
		ASSERT_EQ(info.perf_event.uprobe.offset, offset, "uprobe_offset");

		if (!info.perf_event.uprobe.file_name) {
			ASSERT_EQ(info.perf_event.uprobe.name_len, 0, "name_len");
			info.perf_event.uprobe.file_name = ptr_to_u64(&buf);
			info.perf_event.uprobe.name_len = sizeof(buf);
			goto again;
		}

		err = strncmp(u64_to_ptr(info.perf_event.uprobe.file_name), UPROBE_FILE,
			      strlen(UPROBE_FILE));
			ASSERT_EQ(err, 0, "cmp_file_name");
		break;
	default:
		err = -1;
		break;
	}
	return err;
}

static void kprobe_fill_invalid_user_buffer(int fd)
{
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	int err;

	memset(&info, 0, sizeof(info));

	info.perf_event.kprobe.func_name = 0x1; /* invalid address */
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "invalid_buff_and_len");

	info.perf_event.kprobe.name_len = 64;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EFAULT, "invalid_buff");

	info.perf_event.kprobe.func_name = 0;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "invalid_len");

	ASSERT_EQ(info.perf_event.kprobe.addr, 0, "func_addr");
	ASSERT_EQ(info.perf_event.kprobe.offset, 0, "func_offset");
	ASSERT_EQ(info.perf_event.type, 0, "type");
}

static void test_kprobe_fill_link_info(struct test_fill_link_info *skel,
				       enum bpf_perf_event_type type,
				       bool invalid)
{
	DECLARE_LIBBPF_OPTS(bpf_kprobe_opts, opts,
		.attach_mode = PROBE_ATTACH_MODE_LINK,
		.retprobe = type == BPF_PERF_EVENT_KRETPROBE,
	);
	ssize_t entry_offset = 0;
	int link_fd, err;

	skel->links.kprobe_run = bpf_program__attach_kprobe_opts(skel->progs.kprobe_run,
								 KPROBE_FUNC, &opts);
	if (!ASSERT_OK_PTR(skel->links.kprobe_run, "attach_kprobe"))
		return;

	link_fd = bpf_link__fd(skel->links.kprobe_run);
	if (!invalid) {
		/* See also arch_adjust_kprobe_addr(). */
		if (skel->kconfig->CONFIG_X86_KERNEL_IBT)
			entry_offset = 4;
		err = verify_perf_link_info(link_fd, type, kprobe_addr, 0, entry_offset);
		ASSERT_OK(err, "verify_perf_link_info");
	} else {
		kprobe_fill_invalid_user_buffer(link_fd);
	}
	bpf_link__detach(skel->links.kprobe_run);
}

static void test_tp_fill_link_info(struct test_fill_link_info *skel)
{
	int link_fd, err;

	skel->links.tp_run = bpf_program__attach_tracepoint(skel->progs.tp_run, TP_CAT, TP_NAME);
	if (!ASSERT_OK_PTR(skel->links.tp_run, "attach_tp"))
		return;

	link_fd = bpf_link__fd(skel->links.tp_run);
	err = verify_perf_link_info(link_fd, BPF_PERF_EVENT_TRACEPOINT, 0, 0, 0);
	ASSERT_OK(err, "verify_perf_link_info");
	bpf_link__detach(skel->links.tp_run);
}

static void test_uprobe_fill_link_info(struct test_fill_link_info *skel,
				       enum bpf_perf_event_type type)
{
	int link_fd, err;

	skel->links.uprobe_run = bpf_program__attach_uprobe(skel->progs.uprobe_run,
							    type == BPF_PERF_EVENT_URETPROBE,
							    0, /* self pid */
							    UPROBE_FILE, uprobe_offset);
	if (!ASSERT_OK_PTR(skel->links.uprobe_run, "attach_uprobe"))
		return;

	link_fd = bpf_link__fd(skel->links.uprobe_run);
	err = verify_perf_link_info(link_fd, type, 0, uprobe_offset, 0);
	ASSERT_OK(err, "verify_perf_link_info");
	bpf_link__detach(skel->links.uprobe_run);
}

static int verify_kmulti_link_info(int fd, bool retprobe)
{
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	__u64 addrs[KMULTI_CNT];
	int flags, i, err;

	memset(&info, 0, sizeof(info));

again:
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	if (!ASSERT_OK(err, "get_link_info"))
		return -1;

	if (!ASSERT_EQ(info.type, BPF_LINK_TYPE_KPROBE_MULTI, "kmulti_type"))
		return -1;

	ASSERT_EQ(info.kprobe_multi.count, KMULTI_CNT, "func_cnt");
	flags = info.kprobe_multi.flags & BPF_F_KPROBE_MULTI_RETURN;
	if (!retprobe)
		ASSERT_EQ(flags, 0, "kmulti_flags");
	else
		ASSERT_NEQ(flags, 0, "kretmulti_flags");

	if (!info.kprobe_multi.addrs) {
		info.kprobe_multi.addrs = ptr_to_u64(addrs);
		goto again;
	}
	for (i = 0; i < KMULTI_CNT; i++)
		ASSERT_EQ(addrs[i], kmulti_addrs[i], "kmulti_addrs");
	return 0;
}

static void verify_kmulti_invalid_user_buffer(int fd)
{
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	__u64 addrs[KMULTI_CNT];
	int err, i;

	memset(&info, 0, sizeof(info));

	info.kprobe_multi.count = KMULTI_CNT;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "no_addr");

	info.kprobe_multi.addrs = ptr_to_u64(addrs);
	info.kprobe_multi.count = 0;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "no_cnt");

	for (i = 0; i < KMULTI_CNT; i++)
		addrs[i] = 0;
	info.kprobe_multi.count = KMULTI_CNT - 1;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -ENOSPC, "smaller_cnt");
	for (i = 0; i < KMULTI_CNT - 1; i++)
		ASSERT_EQ(addrs[i], kmulti_addrs[i], "kmulti_addrs");
	ASSERT_EQ(addrs[i], 0, "kmulti_addrs");

	for (i = 0; i < KMULTI_CNT; i++)
		addrs[i] = 0;
	info.kprobe_multi.count = KMULTI_CNT + 1;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, 0, "bigger_cnt");
	for (i = 0; i < KMULTI_CNT; i++)
		ASSERT_EQ(addrs[i], kmulti_addrs[i], "kmulti_addrs");

	info.kprobe_multi.count = KMULTI_CNT;
	info.kprobe_multi.addrs = 0x1; /* invalid addr */
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EFAULT, "invalid_buff");
}

static int symbols_cmp_r(const void *a, const void *b)
{
	const char **str_a = (const char **) a;
	const char **str_b = (const char **) b;

	return strcmp(*str_a, *str_b);
}

static void test_kprobe_multi_fill_link_info(struct test_fill_link_info *skel,
					     bool retprobe, bool invalid)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	int link_fd, err;

	opts.syms = kmulti_syms;
	opts.cnt = KMULTI_CNT;
	opts.retprobe = retprobe;
	skel->links.kmulti_run = bpf_program__attach_kprobe_multi_opts(skel->progs.kmulti_run,
								       NULL, &opts);
	if (!ASSERT_OK_PTR(skel->links.kmulti_run, "attach_kprobe_multi"))
		return;

	link_fd = bpf_link__fd(skel->links.kmulti_run);
	if (!invalid) {
		err = verify_kmulti_link_info(link_fd, retprobe);
		ASSERT_OK(err, "verify_kmulti_link_info");
	} else {
		verify_kmulti_invalid_user_buffer(link_fd);
	}
	bpf_link__detach(skel->links.kmulti_run);
}

void test_fill_link_info(void)
{
	struct test_fill_link_info *skel;
	int i;

	skel = test_fill_link_info__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	/* load kallsyms to compare the addr */
	if (!ASSERT_OK(load_kallsyms_refresh(), "load_kallsyms_refresh"))
		goto cleanup;

	kprobe_addr = ksym_get_addr(KPROBE_FUNC);
	if (test__start_subtest("kprobe_link_info"))
		test_kprobe_fill_link_info(skel, BPF_PERF_EVENT_KPROBE, false);
	if (test__start_subtest("kretprobe_link_info"))
		test_kprobe_fill_link_info(skel, BPF_PERF_EVENT_KRETPROBE, false);
	if (test__start_subtest("kprobe_invalid_ubuff"))
		test_kprobe_fill_link_info(skel, BPF_PERF_EVENT_KPROBE, true);
	if (test__start_subtest("tracepoint_link_info"))
		test_tp_fill_link_info(skel);

	uprobe_offset = get_uprobe_offset(&uprobe_func);
	if (test__start_subtest("uprobe_link_info"))
		test_uprobe_fill_link_info(skel, BPF_PERF_EVENT_UPROBE);
	if (test__start_subtest("uretprobe_link_info"))
		test_uprobe_fill_link_info(skel, BPF_PERF_EVENT_URETPROBE);

	qsort(kmulti_syms, KMULTI_CNT, sizeof(kmulti_syms[0]), symbols_cmp_r);
	for (i = 0; i < KMULTI_CNT; i++)
		kmulti_addrs[i] = ksym_get_addr(kmulti_syms[i]);
	if (test__start_subtest("kprobe_multi_link_info"))
		test_kprobe_multi_fill_link_info(skel, false, false);
	if (test__start_subtest("kretprobe_multi_link_info"))
		test_kprobe_multi_fill_link_info(skel, true, false);
	if (test__start_subtest("kprobe_multi_invalid_ubuff"))
		test_kprobe_multi_fill_link_info(skel, true, true);

cleanup:
	test_fill_link_info__destroy(skel);
}
