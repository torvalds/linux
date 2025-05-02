// SPDX-License-Identifier: GPL-2.0-only
#include <keys/user-type.h>
#include <linux/crash_dump.h>
#include <linux/configfs.h>
#include <linux/module.h>

#define KEY_NUM_MAX 128	/* maximum dm crypt keys */
#define KEY_DESC_MAX_LEN 128	/* maximum dm crypt key description size */

static unsigned int key_count;

struct config_key {
	struct config_item item;
	const char *description;
};

static inline struct config_key *to_config_key(struct config_item *item)
{
	return container_of(item, struct config_key, item);
}

static ssize_t config_key_description_show(struct config_item *item, char *page)
{
	return sprintf(page, "%s\n", to_config_key(item)->description);
}

static ssize_t config_key_description_store(struct config_item *item,
					    const char *page, size_t count)
{
	struct config_key *config_key = to_config_key(item);
	size_t len;
	int ret;

	ret = -EINVAL;
	len = strcspn(page, "\n");

	if (len > KEY_DESC_MAX_LEN) {
		pr_err("The key description shouldn't exceed %u characters", KEY_DESC_MAX_LEN);
		return ret;
	}

	if (!len)
		return ret;

	kfree(config_key->description);
	ret = -ENOMEM;
	config_key->description = kmemdup_nul(page, len, GFP_KERNEL);
	if (!config_key->description)
		return ret;

	return count;
}

CONFIGFS_ATTR(config_key_, description);

static struct configfs_attribute *config_key_attrs[] = {
	&config_key_attr_description,
	NULL,
};

static void config_key_release(struct config_item *item)
{
	kfree(to_config_key(item));
	key_count--;
}

static struct configfs_item_operations config_key_item_ops = {
	.release = config_key_release,
};

static const struct config_item_type config_key_type = {
	.ct_item_ops = &config_key_item_ops,
	.ct_attrs = config_key_attrs,
	.ct_owner = THIS_MODULE,
};

static struct config_item *config_keys_make_item(struct config_group *group,
						 const char *name)
{
	struct config_key *config_key;

	if (key_count > KEY_NUM_MAX) {
		pr_err("Only %u keys at maximum to be created\n", KEY_NUM_MAX);
		return ERR_PTR(-EINVAL);
	}

	config_key = kzalloc(sizeof(struct config_key), GFP_KERNEL);
	if (!config_key)
		return ERR_PTR(-ENOMEM);

	config_item_init_type_name(&config_key->item, name, &config_key_type);

	key_count++;

	return &config_key->item;
}

static ssize_t config_keys_count_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", key_count);
}

CONFIGFS_ATTR_RO(config_keys_, count);

static struct configfs_attribute *config_keys_attrs[] = {
	&config_keys_attr_count,
	NULL,
};

/*
 * Note that, since no extra work is required on ->drop_item(),
 * no ->drop_item() is provided.
 */
static struct configfs_group_operations config_keys_group_ops = {
	.make_item = config_keys_make_item,
};

static const struct config_item_type config_keys_type = {
	.ct_group_ops = &config_keys_group_ops,
	.ct_attrs = config_keys_attrs,
	.ct_owner = THIS_MODULE,
};

static struct configfs_subsystem config_keys_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "crash_dm_crypt_keys",
			.ci_type = &config_keys_type,
		},
	},
};

static int __init configfs_dmcrypt_keys_init(void)
{
	int ret;

	config_group_init(&config_keys_subsys.su_group);
	mutex_init(&config_keys_subsys.su_mutex);
	ret = configfs_register_subsystem(&config_keys_subsys);
	if (ret) {
		pr_err("Error %d while registering subsystem %s\n", ret,
		       config_keys_subsys.su_group.cg_item.ci_namebuf);
		goto out_unregister;
	}

	return 0;

out_unregister:
	configfs_unregister_subsystem(&config_keys_subsys);

	return ret;
}

module_init(configfs_dmcrypt_keys_init);
