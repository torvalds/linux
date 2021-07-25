// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <linux/pkt_cls.h>

#include "test_tc_bpf.skel.h"

#define LO_IFINDEX 1

#define TEST_DECLARE_OPTS(__fd)                                                                   \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_h, .handle = 1);                                     \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_p, .priority = 1);                                   \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_f, .prog_fd = __fd);                                 \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_hp, .handle = 1, .priority = 1);                     \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_hf, .handle = 1, .prog_fd = __fd);                   \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_pf, .priority = 1, .prog_fd = __fd);                 \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_hpf, .handle = 1, .priority = 1, .prog_fd = __fd);   \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_hpi, .handle = 1, .priority = 1, .prog_id = 42);     \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_hpr, .handle = 1, .priority = 1,                     \
			    .flags = BPF_TC_F_REPLACE);                                            \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_hpfi, .handle = 1, .priority = 1, .prog_fd = __fd,   \
			    .prog_id = 42);                                                        \
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts_prio_max, .handle = 1, .priority = UINT16_MAX + 1);

static int test_tc_bpf_basic(const struct bpf_tc_hook *hook, int fd)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 1, .prog_fd = fd);
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int ret;

	ret = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_OK(ret, "bpf_obj_get_info_by_fd"))
		return ret;

	ret = bpf_tc_attach(hook, &opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach"))
		return ret;

	if (!ASSERT_EQ(opts.handle, 1, "handle set") ||
	    !ASSERT_EQ(opts.priority, 1, "priority set") ||
	    !ASSERT_EQ(opts.prog_id, info.id, "prog_id set"))
		goto end;

	opts.prog_id = 0;
	opts.flags = BPF_TC_F_REPLACE;
	ret = bpf_tc_attach(hook, &opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach replace mode"))
		goto end;

	opts.flags = opts.prog_fd = opts.prog_id = 0;
	ret = bpf_tc_query(hook, &opts);
	if (!ASSERT_OK(ret, "bpf_tc_query"))
		goto end;

	if (!ASSERT_EQ(opts.handle, 1, "handle set") ||
	    !ASSERT_EQ(opts.priority, 1, "priority set") ||
	    !ASSERT_EQ(opts.prog_id, info.id, "prog_id set"))
		goto end;

end:
	opts.flags = opts.prog_fd = opts.prog_id = 0;
	ret = bpf_tc_detach(hook, &opts);
	ASSERT_OK(ret, "bpf_tc_detach");
	return ret;
}

static int test_tc_bpf_api(struct bpf_tc_hook *hook, int fd)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, attach_opts, .handle = 1, .priority = 1, .prog_fd = fd);
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, inv_hook, .attach_point = BPF_TC_INGRESS);
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 1);
	int ret;

	ret = bpf_tc_hook_create(NULL);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_create invalid hook = NULL"))
		return -EINVAL;

	/* hook ifindex = 0 */
	ret = bpf_tc_hook_create(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_create invalid hook ifindex == 0"))
		return -EINVAL;

	ret = bpf_tc_hook_destroy(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_destroy invalid hook ifindex == 0"))
		return -EINVAL;

	ret = bpf_tc_attach(&inv_hook, &attach_opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid hook ifindex == 0"))
		return -EINVAL;
	attach_opts.prog_id = 0;

	ret = bpf_tc_detach(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid hook ifindex == 0"))
		return -EINVAL;

	ret = bpf_tc_query(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid hook ifindex == 0"))
		return -EINVAL;

	/* hook ifindex < 0 */
	inv_hook.ifindex = -1;

	ret = bpf_tc_hook_create(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_create invalid hook ifindex < 0"))
		return -EINVAL;

	ret = bpf_tc_hook_destroy(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_destroy invalid hook ifindex < 0"))
		return -EINVAL;

	ret = bpf_tc_attach(&inv_hook, &attach_opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid hook ifindex < 0"))
		return -EINVAL;
	attach_opts.prog_id = 0;

	ret = bpf_tc_detach(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid hook ifindex < 0"))
		return -EINVAL;

	ret = bpf_tc_query(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid hook ifindex < 0"))
		return -EINVAL;

	inv_hook.ifindex = LO_IFINDEX;

	/* hook.attach_point invalid */
	inv_hook.attach_point = 0xabcd;
	ret = bpf_tc_hook_create(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_create invalid hook.attach_point"))
		return -EINVAL;

	ret = bpf_tc_hook_destroy(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_destroy invalid hook.attach_point"))
		return -EINVAL;

	ret = bpf_tc_attach(&inv_hook, &attach_opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid hook.attach_point"))
		return -EINVAL;

	ret = bpf_tc_detach(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid hook.attach_point"))
		return -EINVAL;

	ret = bpf_tc_query(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid hook.attach_point"))
		return -EINVAL;

	inv_hook.attach_point = BPF_TC_INGRESS;

	/* hook.attach_point valid, but parent invalid */
	inv_hook.parent = TC_H_MAKE(1UL << 16, 10);
	ret = bpf_tc_hook_create(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_create invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_hook_destroy(&inv_hook);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_hook_destroy invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_attach(&inv_hook, &attach_opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_detach(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_query(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid hook parent"))
		return -EINVAL;

	inv_hook.attach_point = BPF_TC_CUSTOM;
	inv_hook.parent = 0;
	/* These return EOPNOTSUPP instead of EINVAL as parent is checked after
	 * attach_point of the hook.
	 */
	ret = bpf_tc_hook_create(&inv_hook);
	if (!ASSERT_EQ(ret, -EOPNOTSUPP, "bpf_tc_hook_create invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_hook_destroy(&inv_hook);
	if (!ASSERT_EQ(ret, -EOPNOTSUPP, "bpf_tc_hook_destroy invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_attach(&inv_hook, &attach_opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_detach(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid hook parent"))
		return -EINVAL;

	ret = bpf_tc_query(&inv_hook, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid hook parent"))
		return -EINVAL;

	inv_hook.attach_point = BPF_TC_INGRESS;

	/* detach */
	{
		TEST_DECLARE_OPTS(fd);

		ret = bpf_tc_detach(NULL, &opts_hp);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid hook = NULL"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, NULL);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid opts = NULL"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, &opts_hpr);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid flags set"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, &opts_hpf);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid prog_fd set"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, &opts_hpi);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid prog_id set"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, &opts_p);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid handle unset"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, &opts_h);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid priority unset"))
			return -EINVAL;

		ret = bpf_tc_detach(hook, &opts_prio_max);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid priority > UINT16_MAX"))
			return -EINVAL;
	}

	/* query */
	{
		TEST_DECLARE_OPTS(fd);

		ret = bpf_tc_query(NULL, &opts);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid hook = NULL"))
			return -EINVAL;

		ret = bpf_tc_query(hook, NULL);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid opts = NULL"))
			return -EINVAL;

		ret = bpf_tc_query(hook, &opts_hpr);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid flags set"))
			return -EINVAL;

		ret = bpf_tc_query(hook, &opts_hpf);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid prog_fd set"))
			return -EINVAL;

		ret = bpf_tc_query(hook, &opts_hpi);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid prog_id set"))
			return -EINVAL;

		ret = bpf_tc_query(hook, &opts_p);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid handle unset"))
			return -EINVAL;

		ret = bpf_tc_query(hook, &opts_h);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid priority unset"))
			return -EINVAL;

		ret = bpf_tc_query(hook, &opts_prio_max);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid priority > UINT16_MAX"))
			return -EINVAL;

		/* when chain is not present, kernel returns -EINVAL */
		ret = bpf_tc_query(hook, &opts_hp);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query valid handle, priority set"))
			return -EINVAL;
	}

	/* attach */
	{
		TEST_DECLARE_OPTS(fd);

		ret = bpf_tc_attach(NULL, &opts_hp);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid hook = NULL"))
			return -EINVAL;

		ret = bpf_tc_attach(hook, NULL);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid opts = NULL"))
			return -EINVAL;

		opts_hp.flags = 42;
		ret = bpf_tc_attach(hook, &opts_hp);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid flags"))
			return -EINVAL;

		ret = bpf_tc_attach(hook, NULL);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid prog_fd unset"))
			return -EINVAL;

		ret = bpf_tc_attach(hook, &opts_hpi);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid prog_id set"))
			return -EINVAL;

		ret = bpf_tc_attach(hook, &opts_pf);
		if (!ASSERT_OK(ret, "bpf_tc_attach valid handle unset"))
			return -EINVAL;
		opts_pf.prog_fd = opts_pf.prog_id = 0;
		ASSERT_OK(bpf_tc_detach(hook, &opts_pf), "bpf_tc_detach");

		ret = bpf_tc_attach(hook, &opts_hf);
		if (!ASSERT_OK(ret, "bpf_tc_attach valid priority unset"))
			return -EINVAL;
		opts_hf.prog_fd = opts_hf.prog_id = 0;
		ASSERT_OK(bpf_tc_detach(hook, &opts_hf), "bpf_tc_detach");

		ret = bpf_tc_attach(hook, &opts_prio_max);
		if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid priority > UINT16_MAX"))
			return -EINVAL;

		ret = bpf_tc_attach(hook, &opts_f);
		if (!ASSERT_OK(ret, "bpf_tc_attach valid both handle and priority unset"))
			return -EINVAL;
		opts_f.prog_fd = opts_f.prog_id = 0;
		ASSERT_OK(bpf_tc_detach(hook, &opts_f), "bpf_tc_detach");
	}

	return 0;
}

void test_tc_bpf(void)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook, .ifindex = LO_IFINDEX,
			    .attach_point = BPF_TC_INGRESS);
	struct test_tc_bpf *skel = NULL;
	bool hook_created = false;
	int cls_fd, ret;

	skel = test_tc_bpf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_tc_bpf__open_and_load"))
		return;

	cls_fd = bpf_program__fd(skel->progs.cls);

	ret = bpf_tc_hook_create(&hook);
	if (ret == 0)
		hook_created = true;

	ret = ret == -EEXIST ? 0 : ret;
	if (!ASSERT_OK(ret, "bpf_tc_hook_create(BPF_TC_INGRESS)"))
		goto end;

	hook.attach_point = BPF_TC_CUSTOM;
	hook.parent = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
	ret = bpf_tc_hook_create(&hook);
	if (!ASSERT_EQ(ret, -EOPNOTSUPP, "bpf_tc_hook_create invalid hook.attach_point"))
		goto end;

	ret = test_tc_bpf_basic(&hook, cls_fd);
	if (!ASSERT_OK(ret, "test_tc_internal ingress"))
		goto end;

	ret = bpf_tc_hook_destroy(&hook);
	if (!ASSERT_EQ(ret, -EOPNOTSUPP, "bpf_tc_hook_destroy invalid hook.attach_point"))
		goto end;

	hook.attach_point = BPF_TC_INGRESS;
	hook.parent = 0;
	bpf_tc_hook_destroy(&hook);

	ret = test_tc_bpf_basic(&hook, cls_fd);
	if (!ASSERT_OK(ret, "test_tc_internal ingress"))
		goto end;

	bpf_tc_hook_destroy(&hook);

	hook.attach_point = BPF_TC_EGRESS;
	ret = test_tc_bpf_basic(&hook, cls_fd);
	if (!ASSERT_OK(ret, "test_tc_internal egress"))
		goto end;

	bpf_tc_hook_destroy(&hook);

	ret = test_tc_bpf_api(&hook, cls_fd);
	if (!ASSERT_OK(ret, "test_tc_bpf_api"))
		goto end;

	bpf_tc_hook_destroy(&hook);

end:
	if (hook_created) {
		hook.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS;
		bpf_tc_hook_destroy(&hook);
	}
	test_tc_bpf__destroy(skel);
}
