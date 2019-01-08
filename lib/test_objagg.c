// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/objagg.h>

struct tokey {
	unsigned int id;
};

#define NUM_KEYS 32

static int key_id_index(unsigned int key_id)
{
	if (key_id >= NUM_KEYS) {
		WARN_ON(1);
		return 0;
	}
	return key_id;
}

#define BUF_LEN 128

struct world {
	unsigned int root_count;
	unsigned int delta_count;
	char next_root_buf[BUF_LEN];
	struct objagg_obj *objagg_objs[NUM_KEYS];
	unsigned int key_refs[NUM_KEYS];
};

struct root {
	struct tokey key;
	char buf[BUF_LEN];
};

struct delta {
	unsigned int key_id_diff;
};

static struct objagg_obj *world_obj_get(struct world *world,
					struct objagg *objagg,
					unsigned int key_id)
{
	struct objagg_obj *objagg_obj;
	struct tokey key;
	int err;

	key.id = key_id;
	objagg_obj = objagg_obj_get(objagg, &key);
	if (IS_ERR(objagg_obj)) {
		pr_err("Key %u: Failed to get object.\n", key_id);
		return objagg_obj;
	}
	if (!world->key_refs[key_id_index(key_id)]) {
		world->objagg_objs[key_id_index(key_id)] = objagg_obj;
	} else if (world->objagg_objs[key_id_index(key_id)] != objagg_obj) {
		pr_err("Key %u: God another object for the same key.\n",
		       key_id);
		err = -EINVAL;
		goto err_key_id_check;
	}
	world->key_refs[key_id_index(key_id)]++;
	return objagg_obj;

err_key_id_check:
	objagg_obj_put(objagg, objagg_obj);
	return ERR_PTR(err);
}

static void world_obj_put(struct world *world, struct objagg *objagg,
			  unsigned int key_id)
{
	struct objagg_obj *objagg_obj;

	if (!world->key_refs[key_id_index(key_id)])
		return;
	objagg_obj = world->objagg_objs[key_id_index(key_id)];
	objagg_obj_put(objagg, objagg_obj);
	world->key_refs[key_id_index(key_id)]--;
}

#define MAX_KEY_ID_DIFF 5

static void *delta_create(void *priv, void *parent_obj, void *obj)
{
	struct tokey *parent_key = parent_obj;
	struct world *world = priv;
	struct tokey *key = obj;
	int diff = key->id - parent_key->id;
	struct delta *delta;

	if (diff < 0 || diff > MAX_KEY_ID_DIFF)
		return ERR_PTR(-EINVAL);

	delta = kzalloc(sizeof(*delta), GFP_KERNEL);
	if (!delta)
		return ERR_PTR(-ENOMEM);
	delta->key_id_diff = diff;
	world->delta_count++;
	return delta;
}

static void delta_destroy(void *priv, void *delta_priv)
{
	struct delta *delta = delta_priv;
	struct world *world = priv;

	world->delta_count--;
	kfree(delta);
}

static void *root_create(void *priv, void *obj)
{
	struct world *world = priv;
	struct tokey *key = obj;
	struct root *root;

	root = kzalloc(sizeof(*root), GFP_KERNEL);
	if (!root)
		return ERR_PTR(-ENOMEM);
	memcpy(&root->key, key, sizeof(root->key));
	memcpy(root->buf, world->next_root_buf, sizeof(root->buf));
	world->root_count++;
	return root;
}

static void root_destroy(void *priv, void *root_priv)
{
	struct root *root = root_priv;
	struct world *world = priv;

	world->root_count--;
	kfree(root);
}

static int test_nodelta_obj_get(struct world *world, struct objagg *objagg,
				unsigned int key_id, bool should_create_root)
{
	unsigned int orig_root_count = world->root_count;
	struct objagg_obj *objagg_obj;
	const struct root *root;
	int err;

	if (should_create_root)
		prandom_bytes(world->next_root_buf,
			      sizeof(world->next_root_buf));

	objagg_obj = world_obj_get(world, objagg, key_id);
	if (IS_ERR(objagg_obj)) {
		pr_err("Key %u: Failed to get object.\n", key_id);
		return PTR_ERR(objagg_obj);
	}
	if (should_create_root) {
		if (world->root_count != orig_root_count + 1) {
			pr_err("Key %u: Root was not created\n", key_id);
			err = -EINVAL;
			goto err_check_root_count;
		}
	} else {
		if (world->root_count != orig_root_count) {
			pr_err("Key %u: Root was incorrectly created\n",
			       key_id);
			err = -EINVAL;
			goto err_check_root_count;
		}
	}
	root = objagg_obj_root_priv(objagg_obj);
	if (root->key.id != key_id) {
		pr_err("Key %u: Root has unexpected key id\n", key_id);
		err = -EINVAL;
		goto err_check_key_id;
	}
	if (should_create_root &&
	    memcmp(world->next_root_buf, root->buf, sizeof(root->buf))) {
		pr_err("Key %u: Buffer does not match the expected content\n",
		       key_id);
		err = -EINVAL;
		goto err_check_buf;
	}
	return 0;

err_check_buf:
err_check_key_id:
err_check_root_count:
	objagg_obj_put(objagg, objagg_obj);
	return err;
}

static int test_nodelta_obj_put(struct world *world, struct objagg *objagg,
				unsigned int key_id, bool should_destroy_root)
{
	unsigned int orig_root_count = world->root_count;

	world_obj_put(world, objagg, key_id);

	if (should_destroy_root) {
		if (world->root_count != orig_root_count - 1) {
			pr_err("Key %u: Root was not destroyed\n", key_id);
			return -EINVAL;
		}
	} else {
		if (world->root_count != orig_root_count) {
			pr_err("Key %u: Root was incorrectly destroyed\n",
			       key_id);
			return -EINVAL;
		}
	}
	return 0;
}

static int check_stats_zero(struct objagg *objagg)
{
	const struct objagg_stats *stats;
	int err = 0;

	stats = objagg_stats_get(objagg);
	if (IS_ERR(stats))
		return PTR_ERR(stats);

	if (stats->stats_info_count != 0) {
		pr_err("Stats: Object count is not zero while it should be\n");
		err = -EINVAL;
	}

	objagg_stats_put(stats);
	return err;
}

static int check_stats_nodelta(struct objagg *objagg)
{
	const struct objagg_stats *stats;
	int i;
	int err;

	stats = objagg_stats_get(objagg);
	if (IS_ERR(stats))
		return PTR_ERR(stats);

	if (stats->stats_info_count != NUM_KEYS) {
		pr_err("Stats: Unexpected object count (%u expected, %u returned)\n",
		       NUM_KEYS, stats->stats_info_count);
		err = -EINVAL;
		goto stats_put;
	}

	for (i = 0; i < stats->stats_info_count; i++) {
		if (stats->stats_info[i].stats.user_count != 2) {
			pr_err("Stats: incorrect user count\n");
			err = -EINVAL;
			goto stats_put;
		}
		if (stats->stats_info[i].stats.delta_user_count != 2) {
			pr_err("Stats: incorrect delta user count\n");
			err = -EINVAL;
			goto stats_put;
		}
	}
	err = 0;

stats_put:
	objagg_stats_put(stats);
	return err;
}

static void *delta_create_dummy(void *priv, void *parent_obj, void *obj)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static void delta_destroy_dummy(void *priv, void *delta_priv)
{
}

static const struct objagg_ops nodelta_ops = {
	.obj_size = sizeof(struct tokey),
	.delta_create = delta_create_dummy,
	.delta_destroy = delta_destroy_dummy,
	.root_create = root_create,
	.root_destroy = root_destroy,
};

static int test_nodelta(void)
{
	struct world world = {};
	struct objagg *objagg;
	int i;
	int err;

	objagg = objagg_create(&nodelta_ops, &world);
	if (IS_ERR(objagg))
		return PTR_ERR(objagg);

	err = check_stats_zero(objagg);
	if (err)
		goto err_stats_first_zero;

	/* First round of gets, the root objects should be created */
	for (i = 0; i < NUM_KEYS; i++) {
		err = test_nodelta_obj_get(&world, objagg, i, true);
		if (err)
			goto err_obj_first_get;
	}

	/* Do the second round of gets, all roots are already created,
	 * make sure that no new root is created
	 */
	for (i = 0; i < NUM_KEYS; i++) {
		err = test_nodelta_obj_get(&world, objagg, i, false);
		if (err)
			goto err_obj_second_get;
	}

	err = check_stats_nodelta(objagg);
	if (err)
		goto err_stats_nodelta;

	for (i = NUM_KEYS - 1; i >= 0; i--) {
		err = test_nodelta_obj_put(&world, objagg, i, false);
		if (err)
			goto err_obj_first_put;
	}
	for (i = NUM_KEYS - 1; i >= 0; i--) {
		err = test_nodelta_obj_put(&world, objagg, i, true);
		if (err)
			goto err_obj_second_put;
	}

	err = check_stats_zero(objagg);
	if (err)
		goto err_stats_second_zero;

	objagg_destroy(objagg);
	return 0;

err_stats_nodelta:
err_obj_first_put:
err_obj_second_get:
	for (i--; i >= 0; i--)
		world_obj_put(&world, objagg, i);

	i = NUM_KEYS;
err_obj_first_get:
err_obj_second_put:
	for (i--; i >= 0; i--)
		world_obj_put(&world, objagg, i);
err_stats_first_zero:
err_stats_second_zero:
	objagg_destroy(objagg);
	return err;
}

static const struct objagg_ops delta_ops = {
	.obj_size = sizeof(struct tokey),
	.delta_create = delta_create,
	.delta_destroy = delta_destroy,
	.root_create = root_create,
	.root_destroy = root_destroy,
};

enum action {
	ACTION_GET,
	ACTION_PUT,
};

enum expect_delta {
	EXPECT_DELTA_SAME,
	EXPECT_DELTA_INC,
	EXPECT_DELTA_DEC,
};

enum expect_root {
	EXPECT_ROOT_SAME,
	EXPECT_ROOT_INC,
	EXPECT_ROOT_DEC,
};

struct expect_stats_info {
	struct objagg_obj_stats stats;
	bool is_root;
	unsigned int key_id;
};

struct expect_stats {
	unsigned int info_count;
	struct expect_stats_info info[NUM_KEYS];
};

struct action_item {
	unsigned int key_id;
	enum action action;
	enum expect_delta expect_delta;
	enum expect_root expect_root;
	struct expect_stats expect_stats;
};

#define EXPECT_STATS(count, ...)		\
{						\
	.info_count = count,			\
	.info = { __VA_ARGS__ }			\
}

#define ROOT(key_id, user_count, delta_user_count)	\
	{{user_count, delta_user_count}, true, key_id}

#define DELTA(key_id, user_count)			\
	{{user_count, user_count}, false, key_id}

static const struct action_item action_items[] = {
	{
		1, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_INC,
		EXPECT_STATS(1, ROOT(1, 1, 1)),
	},	/* r: 1			d: */
	{
		7, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_INC,
		EXPECT_STATS(2, ROOT(1, 1, 1), ROOT(7, 1, 1)),
	},	/* r: 1, 7		d: */
	{
		3, ACTION_GET, EXPECT_DELTA_INC, EXPECT_ROOT_SAME,
		EXPECT_STATS(3, ROOT(1, 1, 2), ROOT(7, 1, 1),
				DELTA(3, 1)),
	},	/* r: 1, 7		d: 3^1 */
	{
		5, ACTION_GET, EXPECT_DELTA_INC, EXPECT_ROOT_SAME,
		EXPECT_STATS(4, ROOT(1, 1, 3), ROOT(7, 1, 1),
				DELTA(3, 1), DELTA(5, 1)),
	},	/* r: 1, 7		d: 3^1, 5^1 */
	{
		3, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(4, ROOT(1, 1, 4), ROOT(7, 1, 1),
				DELTA(3, 2), DELTA(5, 1)),
	},	/* r: 1, 7		d: 3^1, 3^1, 5^1 */
	{
		1, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(4, ROOT(1, 2, 5), ROOT(7, 1, 1),
				DELTA(3, 2), DELTA(5, 1)),
	},	/* r: 1, 1, 7		d: 3^1, 3^1, 5^1 */
	{
		30, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_INC,
		EXPECT_STATS(5, ROOT(1, 2, 5), ROOT(7, 1, 1), ROOT(30, 1, 1),
				DELTA(3, 2), DELTA(5, 1)),
	},	/* r: 1, 1, 7, 30	d: 3^1, 3^1, 5^1 */
	{
		8, ACTION_GET, EXPECT_DELTA_INC, EXPECT_ROOT_SAME,
		EXPECT_STATS(6, ROOT(1, 2, 5), ROOT(7, 1, 2), ROOT(30, 1, 1),
				DELTA(3, 2), DELTA(5, 1), DELTA(8, 1)),
	},	/* r: 1, 1, 7, 30	d: 3^1, 3^1, 5^1, 8^7 */
	{
		8, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(6, ROOT(1, 2, 5), ROOT(7, 1, 3), ROOT(30, 1, 1),
				DELTA(3, 2), DELTA(8, 2), DELTA(5, 1)),
	},	/* r: 1, 1, 7, 30	d: 3^1, 3^1, 5^1, 8^7, 8^7 */
	{
		3, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(6, ROOT(1, 2, 4), ROOT(7, 1, 3), ROOT(30, 1, 1),
				DELTA(8, 2), DELTA(3, 1), DELTA(5, 1)),
	},	/* r: 1, 1, 7, 30	d: 3^1, 5^1, 8^7, 8^7 */
	{
		3, ACTION_PUT, EXPECT_DELTA_DEC, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(1, 2, 3), ROOT(7, 1, 3), ROOT(30, 1, 1),
				DELTA(8, 2), DELTA(5, 1)),
	},	/* r: 1, 1, 7, 30	d: 5^1, 8^7, 8^7 */
	{
		1, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(7, 1, 3), ROOT(1, 1, 2), ROOT(30, 1, 1),
				DELTA(8, 2), DELTA(5, 1)),
	},	/* r: 1, 7, 30		d: 5^1, 8^7, 8^7 */
	{
		1, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(7, 1, 3), ROOT(30, 1, 1), ROOT(1, 0, 1),
				DELTA(8, 2), DELTA(5, 1)),
	},	/* r: 7, 30		d: 5^1, 8^7, 8^7 */
	{
		5, ACTION_PUT, EXPECT_DELTA_DEC, EXPECT_ROOT_DEC,
		EXPECT_STATS(3, ROOT(7, 1, 3), ROOT(30, 1, 1),
				DELTA(8, 2)),
	},	/* r: 7, 30		d: 8^7, 8^7 */
	{
		5, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_INC,
		EXPECT_STATS(4, ROOT(7, 1, 3), ROOT(30, 1, 1), ROOT(5, 1, 1),
				DELTA(8, 2)),
	},	/* r: 7, 30, 5		d: 8^7, 8^7 */
	{
		6, ACTION_GET, EXPECT_DELTA_INC, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(7, 1, 3), ROOT(5, 1, 2), ROOT(30, 1, 1),
				DELTA(8, 2), DELTA(6, 1)),
	},	/* r: 7, 30, 5		d: 8^7, 8^7, 6^5 */
	{
		8, ACTION_GET, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(7, 1, 4), ROOT(5, 1, 2), ROOT(30, 1, 1),
				DELTA(8, 3), DELTA(6, 1)),
	},	/* r: 7, 30, 5		d: 8^7, 8^7, 8^7, 6^5 */
	{
		8, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(7, 1, 3), ROOT(5, 1, 2), ROOT(30, 1, 1),
				DELTA(8, 2), DELTA(6, 1)),
	},	/* r: 7, 30, 5		d: 8^7, 8^7, 6^5 */
	{
		8, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(7, 1, 2), ROOT(5, 1, 2), ROOT(30, 1, 1),
				DELTA(8, 1), DELTA(6, 1)),
	},	/* r: 7, 30, 5		d: 8^7, 6^5 */
	{
		8, ACTION_PUT, EXPECT_DELTA_DEC, EXPECT_ROOT_SAME,
		EXPECT_STATS(4, ROOT(5, 1, 2), ROOT(7, 1, 1), ROOT(30, 1, 1),
				DELTA(6, 1)),
	},	/* r: 7, 30, 5		d: 6^5 */
	{
		8, ACTION_GET, EXPECT_DELTA_INC, EXPECT_ROOT_SAME,
		EXPECT_STATS(5, ROOT(5, 1, 3), ROOT(7, 1, 1), ROOT(30, 1, 1),
				DELTA(6, 1), DELTA(8, 1)),
	},	/* r: 7, 30, 5		d: 6^5, 8^5 */
	{
		7, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_DEC,
		EXPECT_STATS(4, ROOT(5, 1, 3), ROOT(30, 1, 1),
				DELTA(6, 1), DELTA(8, 1)),
	},	/* r: 30, 5		d: 6^5, 8^5 */
	{
		30, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_DEC,
		EXPECT_STATS(3, ROOT(5, 1, 3),
				DELTA(6, 1), DELTA(8, 1)),
	},	/* r: 5			d: 6^5, 8^5 */
	{
		5, ACTION_PUT, EXPECT_DELTA_SAME, EXPECT_ROOT_SAME,
		EXPECT_STATS(3, ROOT(5, 0, 2),
				DELTA(6, 1), DELTA(8, 1)),
	},	/* r:			d: 6^5, 8^5 */
	{
		6, ACTION_PUT, EXPECT_DELTA_DEC, EXPECT_ROOT_SAME,
		EXPECT_STATS(2, ROOT(5, 0, 1),
				DELTA(8, 1)),
	},	/* r:			d: 6^5 */
	{
		8, ACTION_PUT, EXPECT_DELTA_DEC, EXPECT_ROOT_DEC,
		EXPECT_STATS(0, ),
	},	/* r:			d: */
};

static int check_expect(struct world *world,
			const struct action_item *action_item,
			unsigned int orig_delta_count,
			unsigned int orig_root_count)
{
	unsigned int key_id = action_item->key_id;

	switch (action_item->expect_delta) {
	case EXPECT_DELTA_SAME:
		if (orig_delta_count != world->delta_count) {
			pr_err("Key %u: Delta count changed while expected to remain the same.\n",
			       key_id);
			return -EINVAL;
		}
		break;
	case EXPECT_DELTA_INC:
		if (WARN_ON(action_item->action == ACTION_PUT))
			return -EINVAL;
		if (orig_delta_count + 1 != world->delta_count) {
			pr_err("Key %u: Delta count was not incremented.\n",
			       key_id);
			return -EINVAL;
		}
		break;
	case EXPECT_DELTA_DEC:
		if (WARN_ON(action_item->action == ACTION_GET))
			return -EINVAL;
		if (orig_delta_count - 1 != world->delta_count) {
			pr_err("Key %u: Delta count was not decremented.\n",
			       key_id);
			return -EINVAL;
		}
		break;
	}

	switch (action_item->expect_root) {
	case EXPECT_ROOT_SAME:
		if (orig_root_count != world->root_count) {
			pr_err("Key %u: Root count changed while expected to remain the same.\n",
			       key_id);
			return -EINVAL;
		}
		break;
	case EXPECT_ROOT_INC:
		if (WARN_ON(action_item->action == ACTION_PUT))
			return -EINVAL;
		if (orig_root_count + 1 != world->root_count) {
			pr_err("Key %u: Root count was not incremented.\n",
			       key_id);
			return -EINVAL;
		}
		break;
	case EXPECT_ROOT_DEC:
		if (WARN_ON(action_item->action == ACTION_GET))
			return -EINVAL;
		if (orig_root_count - 1 != world->root_count) {
			pr_err("Key %u: Root count was not decremented.\n",
			       key_id);
			return -EINVAL;
		}
	}

	return 0;
}

static unsigned int obj_to_key_id(struct objagg_obj *objagg_obj)
{
	const struct tokey *root_key;
	const struct delta *delta;
	unsigned int key_id;

	root_key = objagg_obj_root_priv(objagg_obj);
	key_id = root_key->id;
	delta = objagg_obj_delta_priv(objagg_obj);
	if (delta)
		key_id += delta->key_id_diff;
	return key_id;
}

static int
check_expect_stats_nums(const struct objagg_obj_stats_info *stats_info,
			const struct expect_stats_info *expect_stats_info,
			const char **errmsg)
{
	if (stats_info->is_root != expect_stats_info->is_root) {
		if (errmsg)
			*errmsg = "Incorrect root/delta indication";
		return -EINVAL;
	}
	if (stats_info->stats.user_count !=
	    expect_stats_info->stats.user_count) {
		if (errmsg)
			*errmsg = "Incorrect user count";
		return -EINVAL;
	}
	if (stats_info->stats.delta_user_count !=
	    expect_stats_info->stats.delta_user_count) {
		if (errmsg)
			*errmsg = "Incorrect delta user count";
		return -EINVAL;
	}
	return 0;
}

static int
check_expect_stats_key_id(const struct objagg_obj_stats_info *stats_info,
			  const struct expect_stats_info *expect_stats_info,
			  const char **errmsg)
{
	if (obj_to_key_id(stats_info->objagg_obj) !=
	    expect_stats_info->key_id) {
		if (errmsg)
			*errmsg = "incorrect key id";
		return -EINVAL;
	}
	return 0;
}

static int check_expect_stats_neigh(const struct objagg_stats *stats,
				    const struct expect_stats *expect_stats,
				    int pos)
{
	int i;
	int err;

	for (i = pos - 1; i >= 0; i--) {
		err = check_expect_stats_nums(&stats->stats_info[i],
					      &expect_stats->info[pos], NULL);
		if (err)
			break;
		err = check_expect_stats_key_id(&stats->stats_info[i],
						&expect_stats->info[pos], NULL);
		if (!err)
			return 0;
	}
	for (i = pos + 1; i < stats->stats_info_count; i++) {
		err = check_expect_stats_nums(&stats->stats_info[i],
					      &expect_stats->info[pos], NULL);
		if (err)
			break;
		err = check_expect_stats_key_id(&stats->stats_info[i],
						&expect_stats->info[pos], NULL);
		if (!err)
			return 0;
	}
	return -EINVAL;
}

static int __check_expect_stats(const struct objagg_stats *stats,
				const struct expect_stats *expect_stats,
				const char **errmsg)
{
	int i;
	int err;

	if (stats->stats_info_count != expect_stats->info_count) {
		*errmsg = "Unexpected object count";
		return -EINVAL;
	}

	for (i = 0; i < stats->stats_info_count; i++) {
		err = check_expect_stats_nums(&stats->stats_info[i],
					      &expect_stats->info[i], errmsg);
		if (err)
			return err;
		err = check_expect_stats_key_id(&stats->stats_info[i],
						&expect_stats->info[i], errmsg);
		if (err) {
			/* It is possible that one of the neighbor stats with
			 * same numbers have the correct key id, so check it
			 */
			err = check_expect_stats_neigh(stats, expect_stats, i);
			if (err)
				return err;
		}
	}
	return 0;
}

static int check_expect_stats(struct objagg *objagg,
			      const struct expect_stats *expect_stats,
			      const char **errmsg)
{
	const struct objagg_stats *stats;
	int err;

	stats = objagg_stats_get(objagg);
	if (IS_ERR(stats))
		return PTR_ERR(stats);
	err = __check_expect_stats(stats, expect_stats, errmsg);
	objagg_stats_put(stats);
	return err;
}

static int test_delta_action_item(struct world *world,
				  struct objagg *objagg,
				  const struct action_item *action_item,
				  bool inverse)
{
	unsigned int orig_delta_count = world->delta_count;
	unsigned int orig_root_count = world->root_count;
	unsigned int key_id = action_item->key_id;
	enum action action = action_item->action;
	struct objagg_obj *objagg_obj;
	const char *errmsg;
	int err;

	if (inverse)
		action = action == ACTION_GET ? ACTION_PUT : ACTION_GET;

	switch (action) {
	case ACTION_GET:
		objagg_obj = world_obj_get(world, objagg, key_id);
		if (IS_ERR(objagg_obj))
			return PTR_ERR(objagg_obj);
		break;
	case ACTION_PUT:
		world_obj_put(world, objagg, key_id);
		break;
	}

	if (inverse)
		return 0;
	err = check_expect(world, action_item,
			   orig_delta_count, orig_root_count);
	if (err)
		goto errout;

	errmsg = NULL;
	err = check_expect_stats(objagg, &action_item->expect_stats, &errmsg);
	if (err) {
		pr_err("Key %u: Stats: %s\n", action_item->key_id, errmsg);
		goto errout;
	}

	return 0;

errout:
	/* This can only happen when action is not inversed.
	 * So in case of an error, cleanup by doing inverse action.
	 */
	test_delta_action_item(world, objagg, action_item, true);
	return err;
}

static int test_delta(void)
{
	struct world world = {};
	struct objagg *objagg;
	int i;
	int err;

	objagg = objagg_create(&delta_ops, &world);
	if (IS_ERR(objagg))
		return PTR_ERR(objagg);

	for (i = 0; i < ARRAY_SIZE(action_items); i++) {
		err = test_delta_action_item(&world, objagg,
					     &action_items[i], false);
		if (err)
			goto err_do_action_item;
	}

	objagg_destroy(objagg);
	return 0;

err_do_action_item:
	for (i--; i >= 0; i--)
		test_delta_action_item(&world, objagg, &action_items[i], true);

	objagg_destroy(objagg);
	return err;
}

static int __init test_objagg_init(void)
{
	int err;

	err = test_nodelta();
	if (err)
		return err;
	return test_delta();
}

static void __exit test_objagg_exit(void)
{
}

module_init(test_objagg_init);
module_exit(test_objagg_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Test module for objagg");
