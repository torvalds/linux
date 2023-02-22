// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "kprobe_multi.skel.h"
#include "trace_helpers.h"
#include "kprobe_multi_empty.skel.h"
#include "bpf/libbpf_internal.h"
#include "bpf/hashmap.h"

static void kprobe_multi_test_run(struct kprobe_multi *skel, bool test_return)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	int err, prog_fd;

	prog_fd = bpf_program__fd(skel->progs.trigger);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	ASSERT_EQ(skel->bss->kprobe_test1_result, 1, "kprobe_test1_result");
	ASSERT_EQ(skel->bss->kprobe_test2_result, 1, "kprobe_test2_result");
	ASSERT_EQ(skel->bss->kprobe_test3_result, 1, "kprobe_test3_result");
	ASSERT_EQ(skel->bss->kprobe_test4_result, 1, "kprobe_test4_result");
	ASSERT_EQ(skel->bss->kprobe_test5_result, 1, "kprobe_test5_result");
	ASSERT_EQ(skel->bss->kprobe_test6_result, 1, "kprobe_test6_result");
	ASSERT_EQ(skel->bss->kprobe_test7_result, 1, "kprobe_test7_result");
	ASSERT_EQ(skel->bss->kprobe_test8_result, 1, "kprobe_test8_result");

	if (test_return) {
		ASSERT_EQ(skel->bss->kretprobe_test1_result, 1, "kretprobe_test1_result");
		ASSERT_EQ(skel->bss->kretprobe_test2_result, 1, "kretprobe_test2_result");
		ASSERT_EQ(skel->bss->kretprobe_test3_result, 1, "kretprobe_test3_result");
		ASSERT_EQ(skel->bss->kretprobe_test4_result, 1, "kretprobe_test4_result");
		ASSERT_EQ(skel->bss->kretprobe_test5_result, 1, "kretprobe_test5_result");
		ASSERT_EQ(skel->bss->kretprobe_test6_result, 1, "kretprobe_test6_result");
		ASSERT_EQ(skel->bss->kretprobe_test7_result, 1, "kretprobe_test7_result");
		ASSERT_EQ(skel->bss->kretprobe_test8_result, 1, "kretprobe_test8_result");
	}
}

static void test_skel_api(void)
{
	struct kprobe_multi *skel = NULL;
	int err;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi__open_and_load"))
		goto cleanup;

	skel->bss->pid = getpid();
	err = kprobe_multi__attach(skel);
	if (!ASSERT_OK(err, "kprobe_multi__attach"))
		goto cleanup;

	kprobe_multi_test_run(skel, true);

cleanup:
	kprobe_multi__destroy(skel);
}

static void test_link_api(struct bpf_link_create_opts *opts)
{
	int prog_fd, link1_fd = -1, link2_fd = -1;
	struct kprobe_multi *skel = NULL;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		goto cleanup;

	skel->bss->pid = getpid();
	prog_fd = bpf_program__fd(skel->progs.test_kprobe);
	link1_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_KPROBE_MULTI, opts);
	if (!ASSERT_GE(link1_fd, 0, "link_fd"))
		goto cleanup;

	opts->kprobe_multi.flags = BPF_F_KPROBE_MULTI_RETURN;
	prog_fd = bpf_program__fd(skel->progs.test_kretprobe);
	link2_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_KPROBE_MULTI, opts);
	if (!ASSERT_GE(link2_fd, 0, "link_fd"))
		goto cleanup;

	kprobe_multi_test_run(skel, true);

cleanup:
	if (link1_fd != -1)
		close(link1_fd);
	if (link2_fd != -1)
		close(link2_fd);
	kprobe_multi__destroy(skel);
}

#define GET_ADDR(__sym, __addr) ({					\
	__addr = ksym_get_addr(__sym);					\
	if (!ASSERT_NEQ(__addr, 0, "kallsyms load failed for " #__sym))	\
		return;							\
})

static void test_link_api_addrs(void)
{
	LIBBPF_OPTS(bpf_link_create_opts, opts);
	unsigned long long addrs[8];

	GET_ADDR("bpf_fentry_test1", addrs[0]);
	GET_ADDR("bpf_fentry_test2", addrs[1]);
	GET_ADDR("bpf_fentry_test3", addrs[2]);
	GET_ADDR("bpf_fentry_test4", addrs[3]);
	GET_ADDR("bpf_fentry_test5", addrs[4]);
	GET_ADDR("bpf_fentry_test6", addrs[5]);
	GET_ADDR("bpf_fentry_test7", addrs[6]);
	GET_ADDR("bpf_fentry_test8", addrs[7]);

	opts.kprobe_multi.addrs = (const unsigned long*) addrs;
	opts.kprobe_multi.cnt = ARRAY_SIZE(addrs);
	test_link_api(&opts);
}

static void test_link_api_syms(void)
{
	LIBBPF_OPTS(bpf_link_create_opts, opts);
	const char *syms[8] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test8",
	};

	opts.kprobe_multi.syms = syms;
	opts.kprobe_multi.cnt = ARRAY_SIZE(syms);
	test_link_api(&opts);
}

static void
test_attach_api(const char *pattern, struct bpf_kprobe_multi_opts *opts)
{
	struct bpf_link *link1 = NULL, *link2 = NULL;
	struct kprobe_multi *skel = NULL;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		goto cleanup;

	skel->bss->pid = getpid();
	link1 = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						      pattern, opts);
	if (!ASSERT_OK_PTR(link1, "bpf_program__attach_kprobe_multi_opts"))
		goto cleanup;

	if (opts) {
		opts->retprobe = true;
		link2 = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kretprobe_manual,
							      pattern, opts);
		if (!ASSERT_OK_PTR(link2, "bpf_program__attach_kprobe_multi_opts"))
			goto cleanup;
	}

	kprobe_multi_test_run(skel, !!opts);

cleanup:
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
	kprobe_multi__destroy(skel);
}

static void test_attach_api_pattern(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);

	test_attach_api("bpf_fentry_test*", &opts);
	test_attach_api("bpf_fentry_test?", NULL);
}

static void test_attach_api_addrs(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	unsigned long long addrs[8];

	GET_ADDR("bpf_fentry_test1", addrs[0]);
	GET_ADDR("bpf_fentry_test2", addrs[1]);
	GET_ADDR("bpf_fentry_test3", addrs[2]);
	GET_ADDR("bpf_fentry_test4", addrs[3]);
	GET_ADDR("bpf_fentry_test5", addrs[4]);
	GET_ADDR("bpf_fentry_test6", addrs[5]);
	GET_ADDR("bpf_fentry_test7", addrs[6]);
	GET_ADDR("bpf_fentry_test8", addrs[7]);

	opts.addrs = (const unsigned long *) addrs;
	opts.cnt = ARRAY_SIZE(addrs);
	test_attach_api(NULL, &opts);
}

static void test_attach_api_syms(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	const char *syms[8] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
		"bpf_fentry_test3",
		"bpf_fentry_test4",
		"bpf_fentry_test5",
		"bpf_fentry_test6",
		"bpf_fentry_test7",
		"bpf_fentry_test8",
	};

	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	test_attach_api(NULL, &opts);
}

static void test_attach_api_fails(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	struct kprobe_multi *skel = NULL;
	struct bpf_link *link = NULL;
	unsigned long long addrs[2];
	const char *syms[2] = {
		"bpf_fentry_test1",
		"bpf_fentry_test2",
	};
	__u64 cookies[2];

	addrs[0] = ksym_get_addr("bpf_fentry_test1");
	addrs[1] = ksym_get_addr("bpf_fentry_test2");

	if (!ASSERT_FALSE(!addrs[0] || !addrs[1], "ksym_get_addr"))
		goto cleanup;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		goto cleanup;

	skel->bss->pid = getpid();

	/* fail_1 - pattern and opts NULL */
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     NULL, NULL);
	if (!ASSERT_ERR_PTR(link, "fail_1"))
		goto cleanup;

	if (!ASSERT_EQ(libbpf_get_error(link), -EINVAL, "fail_1_error"))
		goto cleanup;

	/* fail_2 - both addrs and syms set */
	opts.addrs = (const unsigned long *) addrs;
	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     NULL, &opts);
	if (!ASSERT_ERR_PTR(link, "fail_2"))
		goto cleanup;

	if (!ASSERT_EQ(libbpf_get_error(link), -EINVAL, "fail_2_error"))
		goto cleanup;

	/* fail_3 - pattern and addrs set */
	opts.addrs = (const unsigned long *) addrs;
	opts.syms = NULL;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "ksys_*", &opts);
	if (!ASSERT_ERR_PTR(link, "fail_3"))
		goto cleanup;

	if (!ASSERT_EQ(libbpf_get_error(link), -EINVAL, "fail_3_error"))
		goto cleanup;

	/* fail_4 - pattern and cnt set */
	opts.addrs = NULL;
	opts.syms = NULL;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "ksys_*", &opts);
	if (!ASSERT_ERR_PTR(link, "fail_4"))
		goto cleanup;

	if (!ASSERT_EQ(libbpf_get_error(link), -EINVAL, "fail_4_error"))
		goto cleanup;

	/* fail_5 - pattern and cookies */
	opts.addrs = NULL;
	opts.syms = NULL;
	opts.cnt = 0;
	opts.cookies = cookies;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "ksys_*", &opts);
	if (!ASSERT_ERR_PTR(link, "fail_5"))
		goto cleanup;

	if (!ASSERT_EQ(libbpf_get_error(link), -EINVAL, "fail_5_error"))
		goto cleanup;

cleanup:
	bpf_link__destroy(link);
	kprobe_multi__destroy(skel);
}

static inline __u64 get_time_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (__u64) t.tv_sec * 1000000000 + t.tv_nsec;
}

static size_t symbol_hash(const void *key, void *ctx __maybe_unused)
{
	return str_hash((const char *) key);
}

static bool symbol_equal(const void *key1, const void *key2, void *ctx __maybe_unused)
{
	return strcmp((const char *) key1, (const char *) key2) == 0;
}

static int get_syms(char ***symsp, size_t *cntp)
{
	size_t cap = 0, cnt = 0, i;
	char *name = NULL, **syms = NULL;
	struct hashmap *map;
	char buf[256];
	FILE *f;
	int err = 0;

	/*
	 * The available_filter_functions contains many duplicates,
	 * but other than that all symbols are usable in kprobe multi
	 * interface.
	 * Filtering out duplicates by using hashmap__add, which won't
	 * add existing entry.
	 */
	f = fopen("/sys/kernel/debug/tracing/available_filter_functions", "r");
	if (!f)
		return -EINVAL;

	map = hashmap__new(symbol_hash, symbol_equal, NULL);
	if (IS_ERR(map)) {
		err = libbpf_get_error(map);
		goto error;
	}

	while (fgets(buf, sizeof(buf), f)) {
		/* skip modules */
		if (strchr(buf, '['))
			continue;

		free(name);
		if (sscanf(buf, "%ms$*[^\n]\n", &name) != 1)
			continue;
		/*
		 * We attach to almost all kernel functions and some of them
		 * will cause 'suspicious RCU usage' when fprobe is attached
		 * to them. Filter out the current culprits - arch_cpu_idle
		 * default_idle and rcu_* functions.
		 */
		if (!strcmp(name, "arch_cpu_idle"))
			continue;
		if (!strcmp(name, "default_idle"))
			continue;
		if (!strncmp(name, "rcu_", 4))
			continue;
		if (!strcmp(name, "bpf_dispatcher_xdp_func"))
			continue;
		if (!strncmp(name, "__ftrace_invalid_address__",
			     sizeof("__ftrace_invalid_address__") - 1))
			continue;

		err = hashmap__add(map, name, NULL);
		if (err == -EEXIST)
			continue;
		if (err)
			goto error;

		err = libbpf_ensure_mem((void **) &syms, &cap,
					sizeof(*syms), cnt + 1);
		if (err)
			goto error;

		syms[cnt++] = name;
		name = NULL;
	}

	*symsp = syms;
	*cntp = cnt;

error:
	free(name);
	fclose(f);
	hashmap__free(map);
	if (err) {
		for (i = 0; i < cnt; i++)
			free(syms[i]);
		free(syms);
	}
	return err;
}

void serial_test_kprobe_multi_bench_attach(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	struct kprobe_multi_empty *skel = NULL;
	long attach_start_ns, attach_end_ns;
	long detach_start_ns, detach_end_ns;
	double attach_delta, detach_delta;
	struct bpf_link *link = NULL;
	char **syms = NULL;
	size_t cnt = 0, i;

	if (!ASSERT_OK(get_syms(&syms, &cnt), "get_syms"))
		return;

	skel = kprobe_multi_empty__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi_empty__open_and_load"))
		goto cleanup;

	opts.syms = (const char **) syms;
	opts.cnt = cnt;

	attach_start_ns = get_time_ns();
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_empty,
						     NULL, &opts);
	attach_end_ns = get_time_ns();

	if (!ASSERT_OK_PTR(link, "bpf_program__attach_kprobe_multi_opts"))
		goto cleanup;

	detach_start_ns = get_time_ns();
	bpf_link__destroy(link);
	detach_end_ns = get_time_ns();

	attach_delta = (attach_end_ns - attach_start_ns) / 1000000000.0;
	detach_delta = (detach_end_ns - detach_start_ns) / 1000000000.0;

	printf("%s: found %lu functions\n", __func__, cnt);
	printf("%s: attached in %7.3lfs\n", __func__, attach_delta);
	printf("%s: detached in %7.3lfs\n", __func__, detach_delta);

cleanup:
	kprobe_multi_empty__destroy(skel);
	if (syms) {
		for (i = 0; i < cnt; i++)
			free(syms[i]);
		free(syms);
	}
}

void test_kprobe_multi_test(void)
{
	if (!ASSERT_OK(load_kallsyms(), "load_kallsyms"))
		return;

	if (test__start_subtest("skel_api"))
		test_skel_api();
	if (test__start_subtest("link_api_addrs"))
		test_link_api_syms();
	if (test__start_subtest("link_api_syms"))
		test_link_api_addrs();
	if (test__start_subtest("attach_api_pattern"))
		test_attach_api_pattern();
	if (test__start_subtest("attach_api_addrs"))
		test_attach_api_addrs();
	if (test__start_subtest("attach_api_syms"))
		test_attach_api_syms();
	if (test__start_subtest("attach_api_fails"))
		test_attach_api_fails();
}
