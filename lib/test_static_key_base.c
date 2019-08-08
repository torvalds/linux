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
struct static_key base_old_true_key = STATIC_KEY_INIT_TRUE;
EXPORT_SYMBOL_GPL(base_old_true_key);
struct static_key base_inv_old_true_key = STATIC_KEY_INIT_TRUE;
EXPORT_SYMBOL_GPL(base_inv_old_true_key);
struct static_key base_old_false_key = STATIC_KEY_INIT_FALSE;
EXPORT_SYMBOL_GPL(base_old_false_key);
struct static_key base_inv_old_false_key = STATIC_KEY_INIT_FALSE;
EXPORT_SYMBOL_GPL(base_inv_old_false_key);

/* new keys */
DEFINE_STATIC_KEY_TRUE(base_true_key);
EXPORT_SYMBOL_GPL(base_true_key);
DEFINE_STATIC_KEY_TRUE(base_inv_true_key);
EXPORT_SYMBOL_GPL(base_inv_true_key);
DEFINE_STATIC_KEY_FALSE(base_false_key);
EXPORT_SYMBOL_GPL(base_false_key);
DEFINE_STATIC_KEY_FALSE(base_inv_false_key);
EXPORT_SYMBOL_GPL(base_inv_false_key);

static void invert_key(struct static_key *key)
{
	if (static_key_enabled(key))
		static_key_disable(key);
	else
		static_key_enable(key);
}

static int __init test_static_key_base_init(void)
{
	invert_key(&base_inv_old_true_key);
	invert_key(&base_inv_old_false_key);
	invert_key(&base_inv_true_key.key);
	invert_key(&base_inv_false_key.key);

	return 0;
}

static void __exit test_static_key_base_exit(void)
{
}

module_init(test_static_key_base_init);
module_exit(test_static_key_base_exit);

MODULE_AUTHOR("Jason Baron <jbaron@akamai.com>");
MODULE_LICENSE("GPL");
