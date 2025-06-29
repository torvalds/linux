// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include <string.h>
#include <linux/bpf.h>
#include <linux/limits.h>
#include <test_progs.h>
#include "trace_helpers.h"
#include "test_fill_link_info.skel.h"
#include "bpf/libbpf_internal.h"

#define TP_CAT "sched"
#define TP_NAME "sched_switch"

static const char *kmulti_syms[] = {
	"bpf_fentry_test2",
	"bpf_fentry_test1",
	"bpf_fentry_test3",
};
#define KMULTI_CNT ARRAY_SIZE(kmulti_syms)
static __u64 kmulti_addrs[KMULTI_CNT];
static __u64 kmulti_cookies[] = { 3, 1, 2 };

#define KPROBE_FUNC "bpf_fentry_test1"
static __u64 kprobe_addr;

#define UPROBE_FILE "/proc/self/exe"
static ssize_t uprobe_offset;
/* uprobe attach point */
static noinline void uprobe_func(void)
{
	asm volatile ("");
}

#define PERF_EVENT_COOKIE 0xdeadbeef

static int verify_perf_link_info(int fd, enum bpf_perf_event_type type, long addr,
				 ssize_t offset, ssize_t entry_offset)
{
	ssize_t ref_ctr_offset = entry_offset /* ref_ctr_offset for uprobes */;
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

		ASSERT_EQ(info.perf_event.kprobe.cookie, PERF_EVENT_COOKIE, "kprobe_cookie");

		ASSERT_EQ(info.perf_event.kprobe.name_len, strlen(KPROBE_FUNC) + 1,
				  "name_len");
		if (!info.perf_event.kprobe.func_name) {
			info.perf_event.kprobe.func_name = ptr_to_u64(&buf);
			info.perf_event.kprobe.name_len = sizeof(buf);
			goto again;
		}

		err = strncmp(u64_to_ptr(info.perf_event.kprobe.func_name), KPROBE_FUNC,
			      strlen(KPROBE_FUNC));
		ASSERT_EQ(err, 0, "cmp_kprobe_func_name");
		break;
	case BPF_PERF_EVENT_TRACEPOINT:
		ASSERT_EQ(info.perf_event.tracepoint.name_len, strlen(TP_NAME) + 1,
				  "name_len");
		if (!info.perf_event.tracepoint.tp_name) {
			info.perf_event.tracepoint.tp_name = ptr_to_u64(&buf);
			info.perf_event.tracepoint.name_len = sizeof(buf);
			goto again;
		}

		ASSERT_EQ(info.perf_event.tracepoint.cookie, PERF_EVENT_COOKIE, "tracepoint_cookie");

		err = strncmp(u64_to_ptr(info.perf_event.tracepoint.tp_name), TP_NAME,
			      strlen(TP_NAME));
		ASSERT_EQ(err, 0, "cmp_tp_name");
		break;
	case BPF_PERF_EVENT_UPROBE:
	case BPF_PERF_EVENT_URETPROBE:
		ASSERT_EQ(info.perf_event.uprobe.offset, offset, "uprobe_offset");
		ASSERT_EQ(info.perf_event.uprobe.ref_ctr_offset, ref_ctr_offset, "uprobe_ref_ctr_offset");

		ASSERT_EQ(info.perf_event.uprobe.name_len, strlen(UPROBE_FILE) + 1,
				  "name_len");
		if (!info.perf_event.uprobe.file_name) {
			info.perf_event.uprobe.file_name = ptr_to_u64(&buf);
			info.perf_event.uprobe.name_len = sizeof(buf);
			goto again;
		}

		ASSERT_EQ(info.perf_event.uprobe.cookie, PERF_EVENT_COOKIE, "uprobe_cookie");

		err = strncmp(u64_to_ptr(info.perf_event.uprobe.file_name), UPROBE_FILE,
			      strlen(UPROBE_FILE));
			ASSERT_EQ(err, 0, "cmp_file_name");
		break;
	case BPF_PERF_EVENT_EVENT:
		ASSERT_EQ(info.perf_event.event.type, PERF_TYPE_SOFTWARE, "event_type");
		ASSERT_EQ(info.perf_event.event.config, PERF_COUNT_SW_PAGE_FAULTS, "event_config");
		ASSERT_EQ(info.perf_event.event.cookie, PERF_EVENT_COOKIE, "event_cookie");
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
		.bpf_cookie = PERF_EVENT_COOKIE,
	);
	ssize_t entry_offset = 0;
	struct bpf_link *link;
	int link_fd, err;

	link = bpf_program__attach_kprobe_opts(skel->progs.kprobe_run, KPROBE_FUNC, &opts);
	if (!ASSERT_OK_PTR(link, "attach_kprobe"))
		return;

	link_fd = bpf_link__fd(link);
	if (!invalid) {
		/* See also arch_adjust_kprobe_addr(). */
		if (skel->kconfig->CONFIG_X86_KERNEL_IBT)
			entry_offset = 4;
		if (skel->kconfig->CONFIG_PPC64 &&
		    skel->kconfig->CONFIG_KPROBES_ON_FTRACE &&
		    !skel->kconfig->CONFIG_PPC_FTRACE_OUT_OF_LINE)
			entry_offset = 4;
		err = verify_perf_link_info(link_fd, type, kprobe_addr, 0, entry_offset);
		ASSERT_OK(err, "verify_perf_link_info");
	} else {
		kprobe_fill_invalid_user_buffer(link_fd);
	}
	bpf_link__destroy(link);
}

static void test_tp_fill_link_info(struct test_fill_link_info *skel)
{
	DECLARE_LIBBPF_OPTS(bpf_tracepoint_opts, opts,
		.bpf_cookie = PERF_EVENT_COOKIE,
	);
	struct bpf_link *link;
	int link_fd, err;

	link = bpf_program__attach_tracepoint_opts(skel->progs.tp_run, TP_CAT, TP_NAME, &opts);
	if (!ASSERT_OK_PTR(link, "attach_tp"))
		return;

	link_fd = bpf_link__fd(link);
	err = verify_perf_link_info(link_fd, BPF_PERF_EVENT_TRACEPOINT, 0, 0, 0);
	ASSERT_OK(err, "verify_perf_link_info");
	bpf_link__destroy(link);
}

static void test_event_fill_link_info(struct test_fill_link_info *skel)
{
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, opts,
		.bpf_cookie = PERF_EVENT_COOKIE,
	);
	struct bpf_link *link;
	int link_fd, err, pfd;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_PAGE_FAULTS,
		.freq = 1,
		.sample_freq = 1,
		.size = sizeof(struct perf_event_attr),
	};

	pfd = syscall(__NR_perf_event_open, &attr, -1 /* pid */, 0 /* cpu 0 */,
		      -1 /* group id */, 0 /* flags */);
	if (!ASSERT_GE(pfd, 0, "perf_event_open"))
		return;

	link = bpf_program__attach_perf_event_opts(skel->progs.event_run, pfd, &opts);
	if (!ASSERT_OK_PTR(link, "attach_event"))
		goto error;

	link_fd = bpf_link__fd(link);
	err = verify_perf_link_info(link_fd, BPF_PERF_EVENT_EVENT, 0, 0, 0);
	ASSERT_OK(err, "verify_perf_link_info");
	bpf_link__destroy(link);

error:
	close(pfd);
}

static void test_uprobe_fill_link_info(struct test_fill_link_info *skel,
				       enum bpf_perf_event_type type)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, opts,
		.retprobe = type == BPF_PERF_EVENT_URETPROBE,
		.bpf_cookie = PERF_EVENT_COOKIE,
	);
	const char *sema[1] = {
		"uprobe_link_info_sema_1",
	};
	__u64 *ref_ctr_offset;
	struct bpf_link *link;
	int link_fd, err;

	err = elf_resolve_syms_offsets("/proc/self/exe", 1, sema,
				       (unsigned long **) &ref_ctr_offset, STT_OBJECT);
	if (!ASSERT_OK(err, "elf_resolve_syms_offsets_object"))
		return;

	opts.ref_ctr_offset = *ref_ctr_offset;
	link = bpf_program__attach_uprobe_opts(skel->progs.uprobe_run,
					       0, /* self pid */
					       UPROBE_FILE, uprobe_offset,
					       &opts);
	if (!ASSERT_OK_PTR(link, "attach_uprobe"))
		goto out;

	link_fd = bpf_link__fd(link);
	err = verify_perf_link_info(link_fd, type, 0, uprobe_offset, *ref_ctr_offset);
	ASSERT_OK(err, "verify_perf_link_info");
	bpf_link__destroy(link);
out:
	free(ref_ctr_offset);
}

static int verify_kmulti_link_info(int fd, bool retprobe, bool has_cookies)
{
	__u64 addrs[KMULTI_CNT], cookies[KMULTI_CNT];
	struct bpf_link_info info;
	__u32 len = sizeof(info);
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
		info.kprobe_multi.cookies = ptr_to_u64(cookies);
		goto again;
	}
	for (i = 0; i < KMULTI_CNT; i++) {
		ASSERT_EQ(addrs[i], kmulti_addrs[i], "kmulti_addrs");
		ASSERT_EQ(cookies[i], has_cookies ? kmulti_cookies[i] : 0,
			  "kmulti_cookies_value");
	}
	return 0;
}

static void verify_kmulti_invalid_user_buffer(int fd)
{
	__u64 addrs[KMULTI_CNT], cookies[KMULTI_CNT];
	struct bpf_link_info info;
	__u32 len = sizeof(info);
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
	ASSERT_EQ(err, -EFAULT, "invalid_buff_addrs");

	info.kprobe_multi.count = KMULTI_CNT;
	info.kprobe_multi.addrs = ptr_to_u64(addrs);
	info.kprobe_multi.cookies = 0x1; /* invalid addr */
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EFAULT, "invalid_buff_cookies");

	/* cookies && !count */
	info.kprobe_multi.count = 0;
	info.kprobe_multi.addrs = ptr_to_u64(NULL);
	info.kprobe_multi.cookies = ptr_to_u64(cookies);
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "invalid_cookies_count");
}

static int symbols_cmp_r(const void *a, const void *b)
{
	const char **str_a = (const char **) a;
	const char **str_b = (const char **) b;

	return strcmp(*str_a, *str_b);
}

static void test_kprobe_multi_fill_link_info(struct test_fill_link_info *skel,
					     bool retprobe, bool cookies,
					     bool invalid)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	struct bpf_link *link;
	int link_fd, err;

	opts.syms = kmulti_syms;
	opts.cookies = cookies ? kmulti_cookies : NULL;
	opts.cnt = KMULTI_CNT;
	opts.retprobe = retprobe;
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.kmulti_run, NULL, &opts);
	if (!ASSERT_OK_PTR(link, "attach_kprobe_multi"))
		return;

	link_fd = bpf_link__fd(link);
	if (!invalid) {
		err = verify_kmulti_link_info(link_fd, retprobe, cookies);
		ASSERT_OK(err, "verify_kmulti_link_info");
	} else {
		verify_kmulti_invalid_user_buffer(link_fd);
	}
	bpf_link__destroy(link);
}

#define SEC(name) __attribute__((section(name), used))

static short uprobe_link_info_sema_1 SEC(".probes");
static short uprobe_link_info_sema_2 SEC(".probes");
static short uprobe_link_info_sema_3 SEC(".probes");

noinline void uprobe_link_info_func_1(void)
{
	asm volatile ("");
	uprobe_link_info_sema_1++;
}

noinline void uprobe_link_info_func_2(void)
{
	asm volatile ("");
	uprobe_link_info_sema_2++;
}

noinline void uprobe_link_info_func_3(void)
{
	asm volatile ("");
	uprobe_link_info_sema_3++;
}

static int
verify_umulti_link_info(int fd, bool retprobe, __u64 *offsets,
			__u64 *cookies, __u64 *ref_ctr_offsets)
{
	char path[PATH_MAX], path_buf[PATH_MAX];
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	__u64 ref_ctr_offsets_buf[3];
	__u64 offsets_buf[3];
	__u64 cookies_buf[3];
	int i, err, bit;
	__u32 count = 0;

	memset(path, 0, sizeof(path));
	err = readlink("/proc/self/exe", path, sizeof(path));
	if (!ASSERT_NEQ(err, -1, "readlink"))
		return -1;

	memset(&info, 0, sizeof(info));
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_link_get_info_by_fd"))
		return -1;

	ASSERT_EQ(info.uprobe_multi.count, 3, "info.uprobe_multi.count");
	ASSERT_EQ(info.uprobe_multi.path_size, strlen(path) + 1,
		  "info.uprobe_multi.path_size");

	for (bit = 0; bit < 8; bit++) {
		memset(&info, 0, sizeof(info));
		info.uprobe_multi.path = ptr_to_u64(path_buf);
		info.uprobe_multi.path_size = sizeof(path_buf);
		info.uprobe_multi.count = count;

		if (bit & 0x1)
			info.uprobe_multi.offsets = ptr_to_u64(offsets_buf);
		if (bit & 0x2)
			info.uprobe_multi.cookies = ptr_to_u64(cookies_buf);
		if (bit & 0x4)
			info.uprobe_multi.ref_ctr_offsets = ptr_to_u64(ref_ctr_offsets_buf);

		err = bpf_link_get_info_by_fd(fd, &info, &len);
		if (!ASSERT_OK(err, "bpf_link_get_info_by_fd"))
			return -1;

		if (!ASSERT_EQ(info.type, BPF_LINK_TYPE_UPROBE_MULTI, "info.type"))
			return -1;

		ASSERT_EQ(info.uprobe_multi.pid, getpid(), "info.uprobe_multi.pid");
		ASSERT_EQ(info.uprobe_multi.count, 3, "info.uprobe_multi.count");
		ASSERT_EQ(info.uprobe_multi.flags & BPF_F_KPROBE_MULTI_RETURN,
			  retprobe, "info.uprobe_multi.flags.retprobe");
		ASSERT_EQ(info.uprobe_multi.path_size, strlen(path) + 1, "info.uprobe_multi.path_size");
		ASSERT_STREQ(path_buf, path, "info.uprobe_multi.path");

		for (i = 0; i < info.uprobe_multi.count; i++) {
			if (info.uprobe_multi.offsets)
				ASSERT_EQ(offsets_buf[i], offsets[i], "info.uprobe_multi.offsets");
			if (info.uprobe_multi.cookies)
				ASSERT_EQ(cookies_buf[i], cookies[i], "info.uprobe_multi.cookies");
			if (info.uprobe_multi.ref_ctr_offsets) {
				ASSERT_EQ(ref_ctr_offsets_buf[i], ref_ctr_offsets[i],
					  "info.uprobe_multi.ref_ctr_offsets");
			}
		}
		count = count ?: info.uprobe_multi.count;
	}

	return 0;
}

static void verify_umulti_invalid_user_buffer(int fd)
{
	struct bpf_link_info info;
	__u32 len = sizeof(info);
	__u64 buf[3];
	int err;

	/* upath_size defined, not path */
	memset(&info, 0, sizeof(info));
	info.uprobe_multi.path_size = 3;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "failed_upath_size");

	/* path defined, but small */
	memset(&info, 0, sizeof(info));
	info.uprobe_multi.path = ptr_to_u64(buf);
	info.uprobe_multi.path_size = 3;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_LT(err, 0, "failed_upath_small");

	/* path has wrong pointer */
	memset(&info, 0, sizeof(info));
	info.uprobe_multi.path_size = PATH_MAX;
	info.uprobe_multi.path = 123;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EFAULT, "failed_bad_path_ptr");

	/* count zero, with offsets */
	memset(&info, 0, sizeof(info));
	info.uprobe_multi.offsets = ptr_to_u64(buf);
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EINVAL, "failed_count");

	/* offsets not big enough */
	memset(&info, 0, sizeof(info));
	info.uprobe_multi.offsets = ptr_to_u64(buf);
	info.uprobe_multi.count = 2;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -ENOSPC, "failed_small_count");

	/* offsets has wrong pointer */
	memset(&info, 0, sizeof(info));
	info.uprobe_multi.offsets = 123;
	info.uprobe_multi.count = 3;
	err = bpf_link_get_info_by_fd(fd, &info, &len);
	ASSERT_EQ(err, -EFAULT, "failed_wrong_offsets");
}

static void test_uprobe_multi_fill_link_info(struct test_fill_link_info *skel,
					     bool retprobe, bool invalid)
{
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts,
		.retprobe = retprobe,
	);
	const char *syms[3] = {
		"uprobe_link_info_func_1",
		"uprobe_link_info_func_2",
		"uprobe_link_info_func_3",
	};
	__u64 cookies[3] = {
		0xdead,
		0xbeef,
		0xcafe,
	};
	const char *sema[3] = {
		"uprobe_link_info_sema_1",
		"uprobe_link_info_sema_2",
		"uprobe_link_info_sema_3",
	};
	__u64 *offsets = NULL, *ref_ctr_offsets;
	struct bpf_link *link;
	int link_fd, err;

	err = elf_resolve_syms_offsets("/proc/self/exe", 3, sema,
				       (unsigned long **) &ref_ctr_offsets, STT_OBJECT);
	if (!ASSERT_OK(err, "elf_resolve_syms_offsets_object"))
		return;

	err = elf_resolve_syms_offsets("/proc/self/exe", 3, syms,
				       (unsigned long **) &offsets, STT_FUNC);
	if (!ASSERT_OK(err, "elf_resolve_syms_offsets_func"))
		goto out;

	opts.syms = syms;
	opts.cookies = &cookies[0];
	opts.ref_ctr_offsets = (unsigned long *) &ref_ctr_offsets[0];
	opts.cnt = ARRAY_SIZE(syms);

	link = bpf_program__attach_uprobe_multi(skel->progs.umulti_run, 0,
						"/proc/self/exe", NULL, &opts);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach_uprobe_multi"))
		goto out;

	link_fd = bpf_link__fd(link);
	if (invalid)
		verify_umulti_invalid_user_buffer(link_fd);
	else
		verify_umulti_link_info(link_fd, retprobe, offsets, cookies, ref_ctr_offsets);

	bpf_link__destroy(link);
out:
	free(ref_ctr_offsets);
	free(offsets);
}

void test_fill_link_info(void)
{
	struct test_fill_link_info *skel;
	int i;

	skel = test_fill_link_info__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	/* load kallsyms to compare the addr */
	if (!ASSERT_OK(load_kallsyms(), "load_kallsyms"))
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
	if (test__start_subtest("event_link_info"))
		test_event_fill_link_info(skel);

	uprobe_offset = get_uprobe_offset(&uprobe_func);
	if (test__start_subtest("uprobe_link_info"))
		test_uprobe_fill_link_info(skel, BPF_PERF_EVENT_UPROBE);
	if (test__start_subtest("uretprobe_link_info"))
		test_uprobe_fill_link_info(skel, BPF_PERF_EVENT_URETPROBE);

	qsort(kmulti_syms, KMULTI_CNT, sizeof(kmulti_syms[0]), symbols_cmp_r);
	for (i = 0; i < KMULTI_CNT; i++)
		kmulti_addrs[i] = ksym_get_addr(kmulti_syms[i]);
	if (test__start_subtest("kprobe_multi_link_info")) {
		test_kprobe_multi_fill_link_info(skel, false, false, false);
		test_kprobe_multi_fill_link_info(skel, false, true, false);
	}
	if (test__start_subtest("kretprobe_multi_link_info")) {
		test_kprobe_multi_fill_link_info(skel, true, false, false);
		test_kprobe_multi_fill_link_info(skel, true, true, false);
	}
	if (test__start_subtest("kprobe_multi_invalid_ubuff"))
		test_kprobe_multi_fill_link_info(skel, true, true, true);

	if (test__start_subtest("uprobe_multi_link_info"))
		test_uprobe_multi_fill_link_info(skel, false, false);
	if (test__start_subtest("uretprobe_multi_link_info"))
		test_uprobe_multi_fill_link_info(skel, true, false);
	if (test__start_subtest("uprobe_multi_invalid"))
		test_uprobe_multi_fill_link_info(skel, false, true);

cleanup:
	test_fill_link_info__destroy(skel);
}
