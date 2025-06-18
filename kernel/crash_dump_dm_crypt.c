// SPDX-License-Identifier: GPL-2.0-only
#include <linux/key.h>
#include <linux/keyctl.h>
#include <keys/user-type.h>
#include <linux/crash_dump.h>
#include <linux/cc_platform.h>
#include <linux/configfs.h>
#include <linux/module.h>

#define KEY_NUM_MAX 128	/* maximum dm crypt keys */
#define KEY_SIZE_MAX 256	/* maximum dm crypt key size */
#define KEY_DESC_MAX_LEN 128	/* maximum dm crypt key description size */

static unsigned int key_count;

struct dm_crypt_key {
	unsigned int key_size;
	char key_desc[KEY_DESC_MAX_LEN];
	u8 data[KEY_SIZE_MAX];
};

static struct keys_header {
	unsigned int total_keys;
	struct dm_crypt_key keys[] __counted_by(total_keys);
} *keys_header;

static size_t get_keys_header_size(size_t total_keys)
{
	return struct_size(keys_header, keys, total_keys);
}

unsigned long long dm_crypt_keys_addr;
EXPORT_SYMBOL_GPL(dm_crypt_keys_addr);

static int __init setup_dmcryptkeys(char *arg)
{
	char *end;

	if (!arg)
		return -EINVAL;
	dm_crypt_keys_addr = memparse(arg, &end);
	if (end > arg)
		return 0;

	dm_crypt_keys_addr = 0;
	return -EINVAL;
}

early_param("dmcryptkeys", setup_dmcryptkeys);

/*
 * Architectures may override this function to read dm crypt keys
 */
ssize_t __weak dm_crypt_keys_read(char *buf, size_t count, u64 *ppos)
{
	struct kvec kvec = { .iov_base = buf, .iov_len = count };
	struct iov_iter iter;

	iov_iter_kvec(&iter, READ, &kvec, 1, count);
	return read_from_oldmem(&iter, count, ppos, cc_platform_has(CC_ATTR_MEM_ENCRYPT));
}

static int add_key_to_keyring(struct dm_crypt_key *dm_key,
			      key_ref_t keyring_ref)
{
	key_ref_t key_ref;
	int r;

	/* create or update the requested key and add it to the target keyring */
	key_ref = key_create_or_update(keyring_ref, "user", dm_key->key_desc,
				       dm_key->data, dm_key->key_size,
				       KEY_USR_ALL, KEY_ALLOC_IN_QUOTA);

	if (!IS_ERR(key_ref)) {
		r = key_ref_to_ptr(key_ref)->serial;
		key_ref_put(key_ref);
		kexec_dprintk("Success adding key %s", dm_key->key_desc);
	} else {
		r = PTR_ERR(key_ref);
		kexec_dprintk("Error when adding key");
	}

	key_ref_put(keyring_ref);
	return r;
}

static void get_keys_from_kdump_reserved_memory(void)
{
	struct keys_header *keys_header_loaded;

	arch_kexec_unprotect_crashkres();

	keys_header_loaded = kmap_local_page(pfn_to_page(
		kexec_crash_image->dm_crypt_keys_addr >> PAGE_SHIFT));

	memcpy(keys_header, keys_header_loaded, get_keys_header_size(key_count));
	kunmap_local(keys_header_loaded);
	arch_kexec_protect_crashkres();
}

static int restore_dm_crypt_keys_to_thread_keyring(void)
{
	struct dm_crypt_key *key;
	size_t keys_header_size;
	key_ref_t keyring_ref;
	u64 addr;

	/* find the target keyring (which must be writable) */
	keyring_ref =
		lookup_user_key(KEY_SPEC_USER_KEYRING, 0x01, KEY_NEED_WRITE);
	if (IS_ERR(keyring_ref)) {
		kexec_dprintk("Failed to get the user keyring\n");
		return PTR_ERR(keyring_ref);
	}

	addr = dm_crypt_keys_addr;
	dm_crypt_keys_read((char *)&key_count, sizeof(key_count), &addr);
	if (key_count < 0 || key_count > KEY_NUM_MAX) {
		kexec_dprintk("Failed to read the number of dm-crypt keys\n");
		return -1;
	}

	kexec_dprintk("There are %u keys\n", key_count);
	addr = dm_crypt_keys_addr;

	keys_header_size = get_keys_header_size(key_count);
	keys_header = kzalloc(keys_header_size, GFP_KERNEL);
	if (!keys_header)
		return -ENOMEM;

	dm_crypt_keys_read((char *)keys_header, keys_header_size, &addr);

	for (int i = 0; i < keys_header->total_keys; i++) {
		key = &keys_header->keys[i];
		kexec_dprintk("Get key (size=%u)\n", key->key_size);
		add_key_to_keyring(key, keyring_ref);
	}

	return 0;
}

static int read_key_from_user_keying(struct dm_crypt_key *dm_key)
{
	const struct user_key_payload *ukp;
	struct key *key;

	kexec_dprintk("Requesting logon key %s", dm_key->key_desc);
	key = request_key(&key_type_logon, dm_key->key_desc, NULL);

	if (IS_ERR(key)) {
		pr_warn("No such logon key %s\n", dm_key->key_desc);
		return PTR_ERR(key);
	}

	ukp = user_key_payload_locked(key);
	if (!ukp)
		return -EKEYREVOKED;

	if (ukp->datalen > KEY_SIZE_MAX) {
		pr_err("Key size %u exceeds maximum (%u)\n", ukp->datalen, KEY_SIZE_MAX);
		return -EINVAL;
	}

	memcpy(dm_key->data, ukp->data, ukp->datalen);
	dm_key->key_size = ukp->datalen;
	kexec_dprintk("Get dm crypt key (size=%u) %s: %8ph\n", dm_key->key_size,
		      dm_key->key_desc, dm_key->data);
	return 0;
}

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

static bool is_dm_key_reused;

static ssize_t config_keys_reuse_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", is_dm_key_reused);
}

static ssize_t config_keys_reuse_store(struct config_item *item,
					   const char *page, size_t count)
{
	if (!kexec_crash_image || !kexec_crash_image->dm_crypt_keys_addr) {
		kexec_dprintk(
			"dm-crypt keys haven't be saved to crash-reserved memory\n");
		return -EINVAL;
	}

	if (kstrtobool(page, &is_dm_key_reused))
		return -EINVAL;

	if (is_dm_key_reused)
		get_keys_from_kdump_reserved_memory();

	return count;
}

CONFIGFS_ATTR(config_keys_, reuse);

static struct configfs_attribute *config_keys_attrs[] = {
	&config_keys_attr_count,
	&config_keys_attr_reuse,
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

static bool restore;

static ssize_t config_keys_restore_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", restore);
}

static ssize_t config_keys_restore_store(struct config_item *item,
					  const char *page, size_t count)
{
	if (!restore)
		restore_dm_crypt_keys_to_thread_keyring();

	if (kstrtobool(page, &restore))
		return -EINVAL;

	return count;
}

CONFIGFS_ATTR(config_keys_, restore);

static struct configfs_attribute *kdump_config_keys_attrs[] = {
	&config_keys_attr_restore,
	NULL,
};

static const struct config_item_type kdump_config_keys_type = {
	.ct_attrs = kdump_config_keys_attrs,
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

static int build_keys_header(void)
{
	struct config_item *item = NULL;
	struct config_key *key;
	int i, r;

	if (keys_header != NULL)
		kvfree(keys_header);

	keys_header = kzalloc(get_keys_header_size(key_count), GFP_KERNEL);
	if (!keys_header)
		return -ENOMEM;

	keys_header->total_keys = key_count;

	i = 0;
	list_for_each_entry(item, &config_keys_subsys.su_group.cg_children,
			    ci_entry) {
		if (item->ci_type != &config_key_type)
			continue;

		key = to_config_key(item);

		if (!key->description) {
			pr_warn("No key description for key %s\n", item->ci_name);
			return -EINVAL;
		}

		strscpy(keys_header->keys[i].key_desc, key->description,
			KEY_DESC_MAX_LEN);
		r = read_key_from_user_keying(&keys_header->keys[i]);
		if (r != 0) {
			kexec_dprintk("Failed to read key %s\n",
				      keys_header->keys[i].key_desc);
			return r;
		}
		i++;
		kexec_dprintk("Found key: %s\n", item->ci_name);
	}

	return 0;
}

int crash_load_dm_crypt_keys(struct kimage *image)
{
	struct kexec_buf kbuf = {
		.image = image,
		.buf_min = 0,
		.buf_max = ULONG_MAX,
		.top_down = false,
		.random = true,
	};
	int r;


	if (key_count <= 0) {
		kexec_dprintk("No dm-crypt keys\n");
		return -ENOENT;
	}

	if (!is_dm_key_reused) {
		image->dm_crypt_keys_addr = 0;
		r = build_keys_header();
		if (r)
			return r;
	}

	kbuf.buffer = keys_header;
	kbuf.bufsz = get_keys_header_size(key_count);

	kbuf.memsz = kbuf.bufsz;
	kbuf.buf_align = ELF_CORE_HEADER_ALIGN;
	kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
	r = kexec_add_buffer(&kbuf);
	if (r) {
		kvfree((void *)kbuf.buffer);
		return r;
	}
	image->dm_crypt_keys_addr = kbuf.mem;
	image->dm_crypt_keys_sz = kbuf.bufsz;
	kexec_dprintk(
		"Loaded dm crypt keys to kexec_buffer bufsz=0x%lx memsz=0x%lx\n",
		kbuf.bufsz, kbuf.memsz);

	return r;
}

static int __init configfs_dmcrypt_keys_init(void)
{
	int ret;

	if (is_kdump_kernel()) {
		config_keys_subsys.su_group.cg_item.ci_type =
			&kdump_config_keys_type;
	}

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
