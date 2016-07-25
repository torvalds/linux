/*
 * fs/sdcardfs/packagelist.c
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd
 *   Authors: Daeho Jeong, Woojoong Lee, Seunghwan Hyun,
 *               Sunghwan Yun, Sungjong Seo
 *
 * This program has been developed as a stackable file system based on
 * the WrapFS which written by
 *
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009     Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This file is dual licensed.  It may be redistributed and/or modified
 * under the terms of the Apache 2.0 License OR version 2 of the GNU
 * General Public License.
 */

#include "sdcardfs.h"
#include <linux/hashtable.h>
#include <linux/delay.h>


#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/configfs.h>

#define STRING_BUF_SIZE		(512)

struct hashtable_entry {
	struct hlist_node hlist;
	void *key;
	unsigned int value;
};

struct sb_list {
	struct super_block *sb;
	struct list_head list;
};

struct packagelist_data {
	DECLARE_HASHTABLE(package_to_appid,8);
	struct mutex hashtable_lock;

};

static struct packagelist_data *pkgl_data_all;

static struct kmem_cache *hashtable_entry_cachep;

static unsigned int str_hash(const char *key) {
	int i;
	unsigned int h = strlen(key);
	char *data = (char *)key;

	for (i = 0; i < strlen(key); i++) {
		h = h * 31 + *data;
		data++;
	}
	return h;
}

appid_t get_appid(void *pkgl_id, const char *app_name)
{
	struct packagelist_data *pkgl_dat = pkgl_data_all;
	struct hashtable_entry *hash_cur;
	unsigned int hash = str_hash(app_name);
	appid_t ret_id;

	mutex_lock(&pkgl_dat->hashtable_lock);
	hash_for_each_possible(pkgl_dat->package_to_appid, hash_cur, hlist, hash) {
		if (!strcasecmp(app_name, hash_cur->key)) {
			ret_id = (appid_t)hash_cur->value;
			mutex_unlock(&pkgl_dat->hashtable_lock);
			return ret_id;
		}
	}
	mutex_unlock(&pkgl_dat->hashtable_lock);
	return 0;
}

/* Kernel has already enforced everything we returned through
 * derive_permissions_locked(), so this is used to lock down access
 * even further, such as enforcing that apps hold sdcard_rw. */
int check_caller_access_to_name(struct inode *parent_node, const char* name) {

	/* Always block security-sensitive files at root */
	if (parent_node && SDCARDFS_I(parent_node)->perm == PERM_ROOT) {
		if (!strcasecmp(name, "autorun.inf")
			|| !strcasecmp(name, ".android_secure")
			|| !strcasecmp(name, "android_secure")) {
			return 0;
		}
	}

	/* Root always has access; access for any other UIDs should always
	 * be controlled through packages.list. */
	if (from_kuid(&init_user_ns, current_fsuid()) == 0) {
		return 1;
	}

	/* No extra permissions to enforce */
	return 1;
}

/* This function is used when file opening. The open flags must be
 * checked before calling check_caller_access_to_name() */
int open_flags_to_access_mode(int open_flags) {
	if((open_flags & O_ACCMODE) == O_RDONLY) {
		return 0; /* R_OK */
	} else if ((open_flags & O_ACCMODE) == O_WRONLY) {
		return 1; /* W_OK */
	} else {
		/* Probably O_RDRW, but treat as default to be safe */
		return 1; /* R_OK | W_OK */
	}
}

static int insert_str_to_int_lock(struct packagelist_data *pkgl_dat, char *key,
		unsigned int value)
{
	struct hashtable_entry *hash_cur;
	struct hashtable_entry *new_entry;
	unsigned int hash = str_hash(key);

	hash_for_each_possible(pkgl_dat->package_to_appid, hash_cur, hlist, hash) {
		if (!strcasecmp(key, hash_cur->key)) {
			hash_cur->value = value;
			return 0;
		}
	}
	new_entry = kmem_cache_alloc(hashtable_entry_cachep, GFP_KERNEL);
	if (!new_entry)
		return -ENOMEM;
	new_entry->key = kstrdup(key, GFP_KERNEL);
	new_entry->value = value;
	hash_add(pkgl_dat->package_to_appid, &new_entry->hlist, hash);
	return 0;
}

static void fixup_perms(struct super_block *sb) {
	if (sb && sb->s_magic == SDCARDFS_SUPER_MAGIC) {
		mutex_lock(&sb->s_root->d_inode->i_mutex);
		get_derive_permissions_recursive(sb->s_root);
		mutex_unlock(&sb->s_root->d_inode->i_mutex);
	}
}

static int insert_str_to_int(struct packagelist_data *pkgl_dat, char *key,
		unsigned int value) {
	int ret;
	struct sdcardfs_sb_info *sbinfo;
	mutex_lock(&sdcardfs_super_list_lock);
	mutex_lock(&pkgl_dat->hashtable_lock);
	ret = insert_str_to_int_lock(pkgl_dat, key, value);
	mutex_unlock(&pkgl_dat->hashtable_lock);

	list_for_each_entry(sbinfo, &sdcardfs_super_list, list) {
		if (sbinfo) {
			fixup_perms(sbinfo->sb);
		}
	}
	mutex_unlock(&sdcardfs_super_list_lock);
	return ret;
}

static void remove_str_to_int_lock(struct hashtable_entry *h_entry) {
	kfree(h_entry->key);
	hash_del(&h_entry->hlist);
	kmem_cache_free(hashtable_entry_cachep, h_entry);
}

static void remove_str_to_int(struct packagelist_data *pkgl_dat, const char *key)
{
	struct sdcardfs_sb_info *sbinfo;
	struct hashtable_entry *hash_cur;
	unsigned int hash = str_hash(key);
	mutex_lock(&sdcardfs_super_list_lock);
	mutex_lock(&pkgl_dat->hashtable_lock);
	hash_for_each_possible(pkgl_dat->package_to_appid, hash_cur, hlist, hash) {
		if (!strcasecmp(key, hash_cur->key)) {
			remove_str_to_int_lock(hash_cur);
			break;
		}
	}
	mutex_unlock(&pkgl_dat->hashtable_lock);
	list_for_each_entry(sbinfo, &sdcardfs_super_list, list) {
		if (sbinfo) {
			fixup_perms(sbinfo->sb);
		}
	}
	mutex_unlock(&sdcardfs_super_list_lock);
	return;
}

static void remove_all_hashentrys(struct packagelist_data *pkgl_dat)
{
	struct hashtable_entry *hash_cur;
	struct hlist_node *h_t;
	int i;
	mutex_lock(&pkgl_dat->hashtable_lock);
	hash_for_each_safe(pkgl_dat->package_to_appid, i, h_t, hash_cur, hlist)
		remove_str_to_int_lock(hash_cur);
	mutex_unlock(&pkgl_dat->hashtable_lock);
	hash_init(pkgl_dat->package_to_appid);
}

static struct packagelist_data * packagelist_create(void)
{
	struct packagelist_data *pkgl_dat;

	pkgl_dat = kmalloc(sizeof(*pkgl_dat), GFP_KERNEL | __GFP_ZERO);
	if (!pkgl_dat) {
                printk(KERN_ERR "sdcardfs: Failed to create hash\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&pkgl_dat->hashtable_lock);
	hash_init(pkgl_dat->package_to_appid);

	return pkgl_dat;
}

static void packagelist_destroy(struct packagelist_data *pkgl_dat)
{
	remove_all_hashentrys(pkgl_dat);
	printk(KERN_INFO "sdcardfs: destroyed packagelist pkgld\n");
	kfree(pkgl_dat);
}

struct package_appid {
	struct config_item item;
	int add_pid;
};

static inline struct package_appid *to_package_appid(struct config_item *item)
{
	return item ? container_of(item, struct package_appid, item) : NULL;
}

static ssize_t package_appid_attr_show(struct config_item *item,
				      char *page)
{
	ssize_t count;
	count = sprintf(page, "%d\n", get_appid(pkgl_data_all, item->ci_name));
	return count;
}

static ssize_t package_appid_attr_store(struct config_item *item,
				       const char *page, size_t count)
{
	struct package_appid *package_appid = to_package_appid(item);
	unsigned long tmp;
	char *p = (char *) page;
	int ret;

	tmp = simple_strtoul(p, &p, 10);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	if (tmp > INT_MAX)
		return -ERANGE;
	ret = insert_str_to_int(pkgl_data_all, item->ci_name, (unsigned int)tmp);
	package_appid->add_pid = tmp;
	if (ret)
		return ret;

	return count;
}

static struct configfs_attribute package_appid_attr_add_pid = {
	.ca_owner = THIS_MODULE,
	.ca_name = "appid",
	.ca_mode = S_IRUGO | S_IWUGO,
	.show = package_appid_attr_show,
	.store = package_appid_attr_store,
};

static struct configfs_attribute *package_appid_attrs[] = {
	&package_appid_attr_add_pid,
	NULL,
};

static void package_appid_release(struct config_item *item)
{
	printk(KERN_INFO "sdcardfs: removing %s\n", item->ci_dentry->d_name.name);
	/* item->ci_name is freed already, so we rely on the dentry */
	remove_str_to_int(pkgl_data_all, item->ci_dentry->d_name.name);
	kfree(to_package_appid(item));
}

static struct configfs_item_operations package_appid_item_ops = {
	.release		= package_appid_release,
};

static struct config_item_type package_appid_type = {
	.ct_item_ops	= &package_appid_item_ops,
	.ct_attrs	= package_appid_attrs,
	.ct_owner	= THIS_MODULE,
};


struct sdcardfs_packages {
	struct config_group group;
};

static inline struct sdcardfs_packages *to_sdcardfs_packages(struct config_item *item)
{
	return item ? container_of(to_config_group(item), struct sdcardfs_packages, group) : NULL;
}

static struct config_item *sdcardfs_packages_make_item(struct config_group *group, const char *name)
{
	struct package_appid *package_appid;

	package_appid = kzalloc(sizeof(struct package_appid), GFP_KERNEL);
	if (!package_appid)
		return ERR_PTR(-ENOMEM);

	config_item_init_type_name(&package_appid->item, name,
				   &package_appid_type);

	package_appid->add_pid = 0;

	return &package_appid->item;
}

static ssize_t packages_attr_show(struct config_item *item,
					 char *page)
{
	struct hashtable_entry *hash_cur;
	struct hlist_node *h_t;
	int i;
	int count = 0, written = 0;
	char errormsg[] = "<truncated>\n";

	mutex_lock(&pkgl_data_all->hashtable_lock);
	hash_for_each_safe(pkgl_data_all->package_to_appid, i, h_t, hash_cur, hlist) {
		written = scnprintf(page + count, PAGE_SIZE - sizeof(errormsg) - count, "%s %d\n", (char *)hash_cur->key, hash_cur->value);
		if (count + written == PAGE_SIZE - sizeof(errormsg)) {
			count += scnprintf(page + count, PAGE_SIZE - count, errormsg);
			break;
		}
		count += written;
	}
	mutex_unlock(&pkgl_data_all->hashtable_lock);

	return count;
}

static struct configfs_attribute sdcardfs_packages_attr_description = {
	.ca_owner = THIS_MODULE,
	.ca_name = "packages_gid.list",
	.ca_mode = S_IRUGO,
	.show = packages_attr_show,
};

static struct configfs_attribute *sdcardfs_packages_attrs[] = {
	&sdcardfs_packages_attr_description,
	NULL,
};

static void sdcardfs_packages_release(struct config_item *item)
{

	printk(KERN_INFO "sdcardfs: destroyed something?\n");
	kfree(to_sdcardfs_packages(item));
}

static struct configfs_item_operations sdcardfs_packages_item_ops = {
	.release	= sdcardfs_packages_release,
};

/*
 * Note that, since no extra work is required on ->drop_item(),
 * no ->drop_item() is provided.
 */
static struct configfs_group_operations sdcardfs_packages_group_ops = {
	.make_item	= sdcardfs_packages_make_item,
};

static struct config_item_type sdcardfs_packages_type = {
	.ct_item_ops	= &sdcardfs_packages_item_ops,
	.ct_group_ops	= &sdcardfs_packages_group_ops,
	.ct_attrs	= sdcardfs_packages_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem sdcardfs_packages_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "sdcardfs",
			.ci_type = &sdcardfs_packages_type,
		},
	},
};

static int configfs_sdcardfs_init(void)
{
	int ret;
	struct configfs_subsystem *subsys = &sdcardfs_packages_subsys;

	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);
	ret = configfs_register_subsystem(subsys);
	if (ret) {
		printk(KERN_ERR "Error %d while registering subsystem %s\n",
		       ret,
		       subsys->su_group.cg_item.ci_namebuf);
	}
	return ret;
}

static void configfs_sdcardfs_exit(void)
{
	configfs_unregister_subsystem(&sdcardfs_packages_subsys);
}

int packagelist_init(void)
{
	hashtable_entry_cachep =
		kmem_cache_create("packagelist_hashtable_entry",
					sizeof(struct hashtable_entry), 0, 0, NULL);
	if (!hashtable_entry_cachep) {
		printk(KERN_ERR "sdcardfs: failed creating pkgl_hashtable entry slab cache\n");
		return -ENOMEM;
	}

	pkgl_data_all = packagelist_create();
	configfs_sdcardfs_init();
        return 0;
}

void packagelist_exit(void)
{
	configfs_sdcardfs_exit();
	packagelist_destroy(pkgl_data_all);
	if (hashtable_entry_cachep)
		kmem_cache_destroy(hashtable_entry_cachep);
}
