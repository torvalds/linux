// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module for testing static keys.
 *
 * Copyright 2015 Akamai Technologies Inc. All Rights Reserved
 *
 * Authors:
 *      Jason Baron       <jbaron@akamai.com>
 */

#include <linux/module.h>
#include <linux/jump_label.h>

/* old keys */
struct static_key old_true_key	= STATIC_KEY_INIT_TRUE;
struct static_key old_false_key	= STATIC_KEY_INIT_FALSE;

/* new api */
DEFINE_STATIC_KEY_TRUE(true_key);
DEFINE_STATIC_KEY_FALSE(false_key);

/* external */
extern struct static_key base_old_true_key;
extern struct static_key base_inv_old_true_key;
extern struct static_key base_old_false_key;
extern struct static_key base_inv_old_false_key;

/* new api */
extern struct static_key_true base_true_key;
extern struct static_key_true base_inv_true_key;
extern struct static_key_false base_false_key;
extern struct static_key_false base_inv_false_key;


struct test_key {
	bool			init_state;
	struct static_key	*key;
	bool			(*test_key)(void);
};

#define test_key_func(key, branch)	\
static bool key ## _ ## branch(void)	\
{					\
	return branch(&key);		\
}

static void invert_key(struct static_key *key)
{
	if (static_key_enabled(key))
		static_key_disable(key);
	else
		static_key_enable(key);
}

static void invert_keys(struct test_key *keys, int size)
{
	struct static_key *previous = NULL;
	int i;

	for (i = 0; i < size; i++) {
		if (previous != keys[i].key) {
			invert_key(keys[i].key);
			previous = keys[i].key;
		}
	}
}

static int verify_keys(struct test_key *keys, int size, bool invert)
{
	int i;
	bool ret, init;

	for (i = 0; i < size; i++) {
		ret = static_key_enabled(keys[i].key);
		init = keys[i].init_state;
		if (ret != (invert ? !init : init))
			return -EINVAL;
		ret = keys[i].test_key();
		if (static_key_enabled(keys[i].key)) {
			if (!ret)
				return -EINVAL;
		} else {
			if (ret)
				return -EINVAL;
		}
	}
	return 0;
}

test_key_func(old_true_key, static_key_true)
test_key_func(old_false_key, static_key_false)
test_key_func(true_key, static_branch_likely)
test_key_func(true_key, static_branch_unlikely)
test_key_func(false_key, static_branch_likely)
test_key_func(false_key, static_branch_unlikely)
test_key_func(base_old_true_key, static_key_true)
test_key_func(base_inv_old_true_key, static_key_true)
test_key_func(base_old_false_key, static_key_false)
test_key_func(base_inv_old_false_key, static_key_false)
test_key_func(base_true_key, static_branch_likely)
test_key_func(base_true_key, static_branch_unlikely)
test_key_func(base_inv_true_key, static_branch_likely)
test_key_func(base_inv_true_key, static_branch_unlikely)
test_key_func(base_false_key, static_branch_likely)
test_key_func(base_false_key, static_branch_unlikely)
test_key_func(base_inv_false_key, static_branch_likely)
test_key_func(base_inv_false_key, static_branch_unlikely)

static int __init test_static_key_init(void)
{
	int ret;
	int size;

	struct test_key static_key_tests[] = {
		/* internal keys - old keys */
		{
			.init_state	= true,
			.key		= &old_true_key,
			.test_key	= &old_true_key_static_key_true,
		},
		{
			.init_state	= false,
			.key		= &old_false_key,
			.test_key	= &old_false_key_static_key_false,
		},
		/* internal keys - new keys */
		{
			.init_state	= true,
			.key		= &true_key.key,
			.test_key	= &true_key_static_branch_likely,
		},
		{
			.init_state	= true,
			.key		= &true_key.key,
			.test_key	= &true_key_static_branch_unlikely,
		},
		{
			.init_state	= false,
			.key		= &false_key.key,
			.test_key	= &false_key_static_branch_likely,
		},
		{
			.init_state	= false,
			.key		= &false_key.key,
			.test_key	= &false_key_static_branch_unlikely,
		},
		/* external keys - old keys */
		{
			.init_state	= true,
			.key		= &base_old_true_key,
			.test_key	= &base_old_true_key_static_key_true,
		},
		{
			.init_state	= false,
			.key		= &base_inv_old_true_key,
			.test_key	= &base_inv_old_true_key_static_key_true,
		},
		{
			.init_state	= false,
			.key		= &base_old_false_key,
			.test_key	= &base_old_false_key_static_key_false,
		},
		{
			.init_state	= true,
			.key		= &base_inv_old_false_key,
			.test_key	= &base_inv_old_false_key_static_key_false,
		},
		/* external keys - new keys */
		{
			.init_state	= true,
			.key		= &base_true_key.key,
			.test_key	= &base_true_key_static_branch_likely,
		},
		{
			.init_state	= true,
			.key		= &base_true_key.key,
			.test_key	= &base_true_key_static_branch_unlikely,
		},
		{
			.init_state	= false,
			.key		= &base_inv_true_key.key,
			.test_key	= &base_inv_true_key_static_branch_likely,
		},
		{
			.init_state	= false,
			.key		= &base_inv_true_key.key,
			.test_key	= &base_inv_true_key_static_branch_unlikely,
		},
		{
			.init_state	= false,
			.key		= &base_false_key.key,
			.test_key	= &base_false_key_static_branch_likely,
		},
		{
			.init_state	= false,
			.key		= &base_false_key.key,
			.test_key	= &base_false_key_static_branch_unlikely,
		},
		{
			.init_state	= true,
			.key		= &base_inv_false_key.key,
			.test_key	= &base_inv_false_key_static_branch_likely,
		},
		{
			.init_state	= true,
			.key		= &base_inv_false_key.key,
			.test_key	= &base_inv_false_key_static_branch_unlikely,
		},
	};

	size = ARRAY_SIZE(static_key_tests);

	ret = verify_keys(static_key_tests, size, false);
	if (ret)
		goto out;

	invert_keys(static_key_tests, size);
	ret = verify_keys(static_key_tests, size, true);
	if (ret)
		goto out;

	invert_keys(static_key_tests, size);
	ret = verify_keys(static_key_tests, size, false);
	if (ret)
		goto out;
	return 0;
out:
	return ret;
}

static void __exit test_static_key_exit(void)
{
}

module_init(test_static_key_init);
module_exit(test_static_key_exit);

MODULE_AUTHOR("Jason Baron <jbaron@akamai.com>");
MODULE_LICENSE("GPL");
