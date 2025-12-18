// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME " test: " fmt

#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/liveupdate.h>
#include <linux/module.h>
#include "../../kernel/liveupdate/luo_internal.h"

static const struct liveupdate_flb_ops test_flb_ops;
#define DEFINE_TEST_FLB(i) {						\
	.ops = &test_flb_ops,						\
	.compatible = LIVEUPDATE_TEST_FLB_COMPATIBLE(i),		\
}

/* Number of Test FLBs to register with every file handler */
#define TEST_NFLBS 3
static struct liveupdate_flb test_flbs[TEST_NFLBS] = {
	DEFINE_TEST_FLB(0),
	DEFINE_TEST_FLB(1),
	DEFINE_TEST_FLB(2),
};

#define TEST_FLB_MAGIC_BASE 0xFEEDF00DCAFEBEE0ULL

static int test_flb_preserve(struct liveupdate_flb_op_args *argp)
{
	ptrdiff_t index = argp->flb - test_flbs;

	pr_info("%s: preserve was triggered\n", argp->flb->compatible);
	argp->data = TEST_FLB_MAGIC_BASE + index;

	return 0;
}

static void test_flb_unpreserve(struct liveupdate_flb_op_args *argp)
{
	pr_info("%s: unpreserve was triggered\n", argp->flb->compatible);
}

static int test_flb_retrieve(struct liveupdate_flb_op_args *argp)
{
	ptrdiff_t index = argp->flb - test_flbs;
	u64 expected_data = TEST_FLB_MAGIC_BASE + index;

	if (argp->data == expected_data) {
		pr_info("%s: found flb data from the previous boot\n",
			argp->flb->compatible);
		argp->obj = (void *)argp->data;
	} else {
		pr_err("%s: ERROR - incorrect data handle: %llx, expected %llx\n",
		       argp->flb->compatible, argp->data, expected_data);
		return -EINVAL;
	}

	return 0;
}

static void test_flb_finish(struct liveupdate_flb_op_args *argp)
{
	ptrdiff_t index = argp->flb - test_flbs;
	void *expected_obj = (void *)(TEST_FLB_MAGIC_BASE + index);

	if (argp->obj == expected_obj) {
		pr_info("%s: finish was triggered\n", argp->flb->compatible);
	} else {
		pr_err("%s: ERROR - finish called with invalid object\n",
		       argp->flb->compatible);
	}
}

static const struct liveupdate_flb_ops test_flb_ops = {
	.preserve	= test_flb_preserve,
	.unpreserve	= test_flb_unpreserve,
	.retrieve	= test_flb_retrieve,
	.finish		= test_flb_finish,
	.owner		= THIS_MODULE,
};

static void liveupdate_test_init(void)
{
	static DEFINE_MUTEX(init_lock);
	static bool initialized;
	int i;

	guard(mutex)(&init_lock);

	if (initialized)
		return;

	for (i = 0; i < TEST_NFLBS; i++) {
		struct liveupdate_flb *flb = &test_flbs[i];
		void *obj;
		int err;

		err = liveupdate_flb_get_incoming(flb, &obj);
		if (err && err != -ENODATA && err != -ENOENT) {
			pr_err("liveupdate_flb_get_incoming for %s failed: %pe\n",
			       flb->compatible, ERR_PTR(err));
		}
	}
	initialized = true;
}

void liveupdate_test_register(struct liveupdate_file_handler *fh)
{
	int err, i;

	liveupdate_test_init();

	for (i = 0; i < TEST_NFLBS; i++) {
		struct liveupdate_flb *flb = &test_flbs[i];

		err = liveupdate_register_flb(fh, flb);
		if (err) {
			pr_err("Failed to register %s %pe\n",
			       flb->compatible, ERR_PTR(err));
		}
	}

	err = liveupdate_register_flb(fh, &test_flbs[0]);
	if (!err || err != -EEXIST) {
		pr_err("Failed: %s should be already registered, but got err: %pe\n",
		       test_flbs[0].compatible, ERR_PTR(err));
	}

	pr_info("Registered %d FLBs with file handler: [%s]\n",
		TEST_NFLBS, fh->compatible);
}

void liveupdate_test_unregister(struct liveupdate_file_handler *fh)
{
	int err, i;

	for (i = 0; i < TEST_NFLBS; i++) {
		struct liveupdate_flb *flb = &test_flbs[i];

		err = liveupdate_unregister_flb(fh, flb);
		if (err) {
			pr_err("Failed to unregister %s %pe\n",
			       flb->compatible, ERR_PTR(err));
		}
	}

	pr_info("Unregistered %d FLBs from file handler: [%s]\n",
		TEST_NFLBS, fh->compatible);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pasha Tatashin <pasha.tatashin@soleen.com>");
MODULE_DESCRIPTION("In-kernel test for LUO mechanism");
