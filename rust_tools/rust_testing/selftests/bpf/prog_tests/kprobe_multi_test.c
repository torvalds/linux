// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "kprobe_multi.skel.h"
#include "trace_helpers.h"
#include "kprobe_multi_empty.skel.h"
#include "kprobe_multi_override.skel.h"
#include "kprobe_multi_session.skel.h"
#include "kprobe_multi_session_cookie.skel.h"
#include "kprobe_multi_verifier.skel.h"
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
	int saved_error;

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
	saved_error = -errno;
	if (!ASSERT_ERR_PTR(link, "fail_1"))
		goto cleanup;

	if (!ASSERT_EQ(saved_error, -EINVAL, "fail_1_error"))
		goto cleanup;

	/* fail_2 - both addrs and syms set */
	opts.addrs = (const unsigned long *) addrs;
	opts.syms = syms;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     NULL, &opts);
	saved_error = -errno;
	if (!ASSERT_ERR_PTR(link, "fail_2"))
		goto cleanup;

	if (!ASSERT_EQ(saved_error, -EINVAL, "fail_2_error"))
		goto cleanup;

	/* fail_3 - pattern and addrs set */
	opts.addrs = (const unsigned long *) addrs;
	opts.syms = NULL;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "ksys_*", &opts);
	saved_error = -errno;
	if (!ASSERT_ERR_PTR(link, "fail_3"))
		goto cleanup;

	if (!ASSERT_EQ(saved_error, -EINVAL, "fail_3_error"))
		goto cleanup;

	/* fail_4 - pattern and cnt set */
	opts.addrs = NULL;
	opts.syms = NULL;
	opts.cnt = ARRAY_SIZE(syms);
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "ksys_*", &opts);
	saved_error = -errno;
	if (!ASSERT_ERR_PTR(link, "fail_4"))
		goto cleanup;

	if (!ASSERT_EQ(saved_error, -EINVAL, "fail_4_error"))
		goto cleanup;

	/* fail_5 - pattern and cookies */
	opts.addrs = NULL;
	opts.syms = NULL;
	opts.cnt = 0;
	opts.cookies = cookies;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "ksys_*", &opts);
	saved_error = -errno;
	if (!ASSERT_ERR_PTR(link, "fail_5"))
		goto cleanup;

	if (!ASSERT_EQ(saved_error, -EINVAL, "fail_5_error"))
		goto cleanup;

	/* fail_6 - abnormal cnt */
	opts.addrs = (const unsigned long *) addrs;
	opts.syms = NULL;
	opts.cnt = INT_MAX;
	opts.cookies = NULL;

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     NULL, &opts);
	saved_error = -errno;
	if (!ASSERT_ERR_PTR(link, "fail_6"))
		goto cleanup;

	if (!ASSERT_EQ(saved_error, -E2BIG, "fail_6_error"))
		goto cleanup;

cleanup:
	bpf_link__destroy(link);
	kprobe_multi__destroy(skel);
}

static void test_session_skel_api(void)
{
	struct kprobe_multi_session *skel = NULL;
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_link *link = NULL;
	int i, err, prog_fd;

	skel = kprobe_multi_session__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi_session__open_and_load"))
		return;

	skel->bss->pid = getpid();

	err = kprobe_multi_session__attach(skel);
	if (!ASSERT_OK(err, " kprobe_multi_session__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.trigger);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	/* bpf_fentry_test1-4 trigger return probe, result is 2 */
	for (i = 0; i < 4; i++)
		ASSERT_EQ(skel->bss->kprobe_session_result[i], 2, "kprobe_session_result");

	/* bpf_fentry_test5-8 trigger only entry probe, result is 1 */
	for (i = 4; i < 8; i++)
		ASSERT_EQ(skel->bss->kprobe_session_result[i], 1, "kprobe_session_result");

cleanup:
	bpf_link__destroy(link);
	kprobe_multi_session__destroy(skel);
}

static void test_session_cookie_skel_api(void)
{
	struct kprobe_multi_session_cookie *skel = NULL;
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	LIBBPF_OPTS(bpf_test_run_opts, topts);
	struct bpf_link *link = NULL;
	int err, prog_fd;

	skel = kprobe_multi_session_cookie__open_and_load();
	if (!ASSERT_OK_PTR(skel, "fentry_raw_skel_load"))
		return;

	skel->bss->pid = getpid();

	err = kprobe_multi_session_cookie__attach(skel);
	if (!ASSERT_OK(err, " kprobe_multi_wrapper__attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.trigger);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	ASSERT_EQ(skel->bss->test_kprobe_1_result, 1, "test_kprobe_1_result");
	ASSERT_EQ(skel->bss->test_kprobe_2_result, 2, "test_kprobe_2_result");
	ASSERT_EQ(skel->bss->test_kprobe_3_result, 3, "test_kprobe_3_result");

cleanup:
	bpf_link__destroy(link);
	kprobe_multi_session_cookie__destroy(skel);
}

static void test_unique_match(void)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	struct kprobe_multi *skel = NULL;
	struct bpf_link *link = NULL;

	skel = kprobe_multi__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi__open_and_load"))
		return;

	opts.unique_match = true;
	skel->bss->pid = getpid();
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "bpf_fentry_test*", &opts);
	if (!ASSERT_ERR_PTR(link, "bpf_program__attach_kprobe_multi_opts"))
		bpf_link__destroy(link);

	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_manual,
						     "bpf_fentry_test8*", &opts);
	if (ASSERT_OK_PTR(link, "bpf_program__attach_kprobe_multi_opts"))
		bpf_link__destroy(link);

	kprobe_multi__destroy(skel);
}

static size_t symbol_hash(long key, void *ctx __maybe_unused)
{
	return str_hash((const char *) key);
}

static bool symbol_equal(long key1, long key2, void *ctx __maybe_unused)
{
	return strcmp((const char *) key1, (const char *) key2) == 0;
}

static bool is_invalid_entry(char *buf, bool kernel)
{
	if (kernel && strchr(buf, '['))
		return true;
	if (!kernel && !strchr(buf, '['))
		return true;
	return false;
}

static bool skip_entry(char *name)
{
	/*
	 * We attach to almost all kernel functions and some of them
	 * will cause 'suspicious RCU usage' when fprobe is attached
	 * to them. Filter out the current culprits - arch_cpu_idle
	 * default_idle and rcu_* functions.
	 */
	if (!strcmp(name, "arch_cpu_idle"))
		return true;
	if (!strcmp(name, "default_idle"))
		return true;
	if (!strncmp(name, "rcu_", 4))
		return true;
	if (!strcmp(name, "bpf_dispatcher_xdp_func"))
		return true;
	if (!strncmp(name, "__ftrace_invalid_address__",
		     sizeof("__ftrace_invalid_address__") - 1))
		return true;
	return false;
}

/* Do comparision by ignoring '.llvm.<hash>' suffixes. */
static int compare_name(const char *name1, const char *name2)
{
	const char *res1, *res2;
	int len1, len2;

	res1 = strstr(name1, ".llvm.");
	res2 = strstr(name2, ".llvm.");
	len1 = res1 ? res1 - name1 : strlen(name1);
	len2 = res2 ? res2 - name2 : strlen(name2);

	if (len1 == len2)
		return strncmp(name1, name2, len1);
	if (len1 < len2)
		return strncmp(name1, name2, len1) <= 0 ? -1 : 1;
	return strncmp(name1, name2, len2) >= 0 ? 1 : -1;
}

static int load_kallsyms_compare(const void *p1, const void *p2)
{
	return compare_name(((const struct ksym *)p1)->name, ((const struct ksym *)p2)->name);
}

static int search_kallsyms_compare(const void *p1, const struct ksym *p2)
{
	return compare_name(p1, p2->name);
}

static int get_syms(char ***symsp, size_t *cntp, bool kernel)
{
	size_t cap = 0, cnt = 0;
	char *name = NULL, *ksym_name, **syms = NULL;
	struct hashmap *map;
	struct ksyms *ksyms;
	struct ksym *ks;
	char buf[256];
	FILE *f;
	int err = 0;

	ksyms = load_kallsyms_custom_local(load_kallsyms_compare);
	if (!ASSERT_OK_PTR(ksyms, "load_kallsyms_custom_local"))
		return -EINVAL;

	/*
	 * The available_filter_functions contains many duplicates,
	 * but other than that all symbols are usable in kprobe multi
	 * interface.
	 * Filtering out duplicates by using hashmap__add, which won't
	 * add existing entry.
	 */

	if (access("/sys/kernel/tracing/trace", F_OK) == 0)
		f = fopen("/sys/kernel/tracing/available_filter_functions", "r");
	else
		f = fopen("/sys/kernel/debug/tracing/available_filter_functions", "r");

	if (!f)
		return -EINVAL;

	map = hashmap__new(symbol_hash, symbol_equal, NULL);
	if (IS_ERR(map)) {
		err = libbpf_get_error(map);
		goto error;
	}

	while (fgets(buf, sizeof(buf), f)) {
		if (is_invalid_entry(buf, kernel))
			continue;

		free(name);
		if (sscanf(buf, "%ms$*[^\n]\n", &name) != 1)
			continue;
		if (skip_entry(name))
			continue;

		ks = search_kallsyms_custom_local(ksyms, name, search_kallsyms_compare);
		if (!ks) {
			err = -EINVAL;
			goto error;
		}

		ksym_name = ks->name;
		err = hashmap__add(map, ksym_name, 0);
		if (err == -EEXIST) {
			err = 0;
			continue;
		}
		if (err)
			goto error;

		err = libbpf_ensure_mem((void **) &syms, &cap,
					sizeof(*syms), cnt + 1);
		if (err)
			goto error;

		syms[cnt++] = ksym_name;
	}

	*symsp = syms;
	*cntp = cnt;

error:
	free(name);
	fclose(f);
	hashmap__free(map);
	if (err)
		free(syms);
	return err;
}

static int get_addrs(unsigned long **addrsp, size_t *cntp, bool kernel)
{
	unsigned long *addr, *addrs, *tmp_addrs;
	int err = 0, max_cnt, inc_cnt;
	char *name = NULL;
	size_t cnt = 0;
	char buf[256];
	FILE *f;

	if (access("/sys/kernel/tracing/trace", F_OK) == 0)
		f = fopen("/sys/kernel/tracing/available_filter_functions_addrs", "r");
	else
		f = fopen("/sys/kernel/debug/tracing/available_filter_functions_addrs", "r");

	if (!f)
		return -ENOENT;

	/* In my local setup, the number of entries is 50k+ so Let us initially
	 * allocate space to hold 64k entries. If 64k is not enough, incrementally
	 * increase 1k each time.
	 */
	max_cnt = 65536;
	inc_cnt = 1024;
	addrs = malloc(max_cnt * sizeof(long));
	if (addrs == NULL) {
		err = -ENOMEM;
		goto error;
	}

	while (fgets(buf, sizeof(buf), f)) {
		if (is_invalid_entry(buf, kernel))
			continue;

		free(name);
		if (sscanf(buf, "%p %ms$*[^\n]\n", &addr, &name) != 2)
			continue;
		if (skip_entry(name))
			continue;

		if (cnt == max_cnt) {
			max_cnt += inc_cnt;
			tmp_addrs = realloc(addrs, max_cnt);
			if (!tmp_addrs) {
				err = -ENOMEM;
				goto error;
			}
			addrs = tmp_addrs;
		}

		addrs[cnt++] = (unsigned long)addr;
	}

	*addrsp = addrs;
	*cntp = cnt;

error:
	free(name);
	fclose(f);
	if (err)
		free(addrs);
	return err;
}

static void do_bench_test(struct kprobe_multi_empty *skel, struct bpf_kprobe_multi_opts *opts)
{
	long attach_start_ns, attach_end_ns;
	long detach_start_ns, detach_end_ns;
	double attach_delta, detach_delta;
	struct bpf_link *link = NULL;

	attach_start_ns = get_time_ns();
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_kprobe_empty,
						     NULL, opts);
	attach_end_ns = get_time_ns();

	if (!ASSERT_OK_PTR(link, "bpf_program__attach_kprobe_multi_opts"))
		return;

	detach_start_ns = get_time_ns();
	bpf_link__destroy(link);
	detach_end_ns = get_time_ns();

	attach_delta = (attach_end_ns - attach_start_ns) / 1000000000.0;
	detach_delta = (detach_end_ns - detach_start_ns) / 1000000000.0;

	printf("%s: found %lu functions\n", __func__, opts->cnt);
	printf("%s: attached in %7.3lfs\n", __func__, attach_delta);
	printf("%s: detached in %7.3lfs\n", __func__, detach_delta);
}

static void test_kprobe_multi_bench_attach(bool kernel)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	struct kprobe_multi_empty *skel = NULL;
	char **syms = NULL;
	size_t cnt = 0;

	if (!ASSERT_OK(get_syms(&syms, &cnt, kernel), "get_syms"))
		return;

	skel = kprobe_multi_empty__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi_empty__open_and_load"))
		goto cleanup;

	opts.syms = (const char **) syms;
	opts.cnt = cnt;

	do_bench_test(skel, &opts);

cleanup:
	kprobe_multi_empty__destroy(skel);
	if (syms)
		free(syms);
}

static void test_kprobe_multi_bench_attach_addr(bool kernel)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	struct kprobe_multi_empty *skel = NULL;
	unsigned long *addrs = NULL;
	size_t cnt = 0;
	int err;

	err = get_addrs(&addrs, &cnt, kernel);
	if (err == -ENOENT) {
		test__skip();
		return;
	}

	if (!ASSERT_OK(err, "get_addrs"))
		return;

	skel = kprobe_multi_empty__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi_empty__open_and_load"))
		goto cleanup;

	opts.addrs = addrs;
	opts.cnt = cnt;

	do_bench_test(skel, &opts);

cleanup:
	kprobe_multi_empty__destroy(skel);
	free(addrs);
}

static void test_attach_override(void)
{
	struct kprobe_multi_override *skel = NULL;
	struct bpf_link *link = NULL;

	skel = kprobe_multi_override__open_and_load();
	if (!ASSERT_OK_PTR(skel, "kprobe_multi_empty__open_and_load"))
		goto cleanup;

	/* The test_override calls bpf_override_return so it should fail
	 * to attach to bpf_fentry_test1 function, which is not on error
	 * injection list.
	 */
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_override,
						     "bpf_fentry_test1", NULL);
	if (!ASSERT_ERR_PTR(link, "override_attached_bpf_fentry_test1")) {
		bpf_link__destroy(link);
		goto cleanup;
	}

	/* The should_fail_bio function is on error injection list,
	 * attach should succeed.
	 */
	link = bpf_program__attach_kprobe_multi_opts(skel->progs.test_override,
						     "should_fail_bio", NULL);
	if (!ASSERT_OK_PTR(link, "override_attached_should_fail_bio"))
		goto cleanup;

	bpf_link__destroy(link);

cleanup:
	kprobe_multi_override__destroy(skel);
}

void serial_test_kprobe_multi_bench_attach(void)
{
	if (test__start_subtest("kernel"))
		test_kprobe_multi_bench_attach(true);
	if (test__start_subtest("modules"))
		test_kprobe_multi_bench_attach(false);
	if (test__start_subtest("kernel"))
		test_kprobe_multi_bench_attach_addr(true);
	if (test__start_subtest("modules"))
		test_kprobe_multi_bench_attach_addr(false);
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
	if (test__start_subtest("attach_override"))
		test_attach_override();
	if (test__start_subtest("session"))
		test_session_skel_api();
	if (test__start_subtest("session_cookie"))
		test_session_cookie_skel_api();
	if (test__start_subtest("unique_match"))
		test_unique_match();
	RUN_TESTS(kprobe_multi_verifier);
}
