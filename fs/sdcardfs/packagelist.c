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
#include "strtok.h"
#include "hashtable.h"
#include <linux/syscalls.h>
#include <linux/kthread.h>
#include <linux/inotify.h>
#include <linux/delay.h>

#define STRING_BUF_SIZE		(512)

struct hashtable_entry {
        struct hlist_node hlist;
        void *key;
	int value;
};

struct packagelist_data {
	DECLARE_HASHTABLE(package_to_appid,8);
	DECLARE_HASHTABLE(appid_with_rw,7);
	struct mutex hashtable_lock;
	struct task_struct *thread_id;
	gid_t write_gid;
	char *strtok_last;
	char read_buf[STRING_BUF_SIZE];
	char event_buf[STRING_BUF_SIZE];
	char app_name_buf[STRING_BUF_SIZE];
	char gids_buf[STRING_BUF_SIZE];
};

static struct kmem_cache *hashtable_entry_cachep;

/* Path to system-provided mapping of package name to appIds */
static const char* const kpackageslist_file = "/data/system/packages.list";
/* Supplementary groups to execute with */
static const gid_t kgroups[1] = { AID_PACKAGE_INFO };

static unsigned int str_hash(void *key) {
	int i;
	unsigned int h = strlen(key);
	char *data = (char *)key;

	for (i = 0; i < strlen(key); i++) {
		h = h * 31 + *data;
		data++;
	}
	return h;
}

static int contain_appid_key(struct packagelist_data *pkgl_dat, void *appid) {
        struct hashtable_entry *hash_cur;
	struct hlist_node *h_n;

        hash_for_each_possible(pkgl_dat->appid_with_rw,	hash_cur, hlist, (unsigned int)appid, h_n)
                if (appid == hash_cur->key)
                        return 1;
	return 0;
}

/* Return if the calling UID holds sdcard_rw. */
int get_caller_has_rw_locked(void *pkgl_id, derive_t derive) {
	struct packagelist_data *pkgl_dat = (struct packagelist_data *)pkgl_id;
	appid_t appid;
	int ret;

	/* No additional permissions enforcement */
	if (derive == DERIVE_NONE) {
		return 1;
	}

	appid = multiuser_get_app_id(current_fsuid());
	mutex_lock(&pkgl_dat->hashtable_lock);
	ret = contain_appid_key(pkgl_dat, (void *)appid);
	mutex_unlock(&pkgl_dat->hashtable_lock);
	return ret;
}

appid_t get_appid(void *pkgl_id, const char *app_name)
{
	struct packagelist_data *pkgl_dat = (struct packagelist_data *)pkgl_id;
	struct hashtable_entry *hash_cur;
	struct hlist_node *h_n;
	unsigned int hash = str_hash((void *)app_name);
	appid_t ret_id;

	//printk(KERN_INFO "sdcardfs: %s: %s, %u\n", __func__, (char *)app_name, hash);
	mutex_lock(&pkgl_dat->hashtable_lock);
	hash_for_each_possible(pkgl_dat->package_to_appid, hash_cur, hlist, hash, h_n) {
		//printk(KERN_INFO "sdcardfs: %s: %s\n", __func__, (char *)hash_cur->key);
		if (!strcasecmp(app_name, hash_cur->key)) {
			ret_id = (appid_t)hash_cur->value;
			mutex_unlock(&pkgl_dat->hashtable_lock);
			//printk(KERN_INFO "=> app_id: %d\n", (int)ret_id);
			return ret_id;
		}
	}
	mutex_unlock(&pkgl_dat->hashtable_lock);
	//printk(KERN_INFO "=> app_id: %d\n", 0);
	return 0;
}

/* Kernel has already enforced everything we returned through
 * derive_permissions_locked(), so this is used to lock down access
 * even further, such as enforcing that apps hold sdcard_rw. */
int check_caller_access_to_name(struct inode *parent_node, const char* name,
					derive_t derive, int w_ok, int has_rw) {

	/* Always block security-sensitive files at root */
	if (parent_node && SDCARDFS_I(parent_node)->perm == PERM_ROOT) {
		if (!strcasecmp(name, "autorun.inf")
			|| !strcasecmp(name, ".android_secure")
			|| !strcasecmp(name, "android_secure")) {
			return 0;
		}
	}

	/* No additional permissions enforcement */
	if (derive == DERIVE_NONE) {
		return 1;
	}

	/* Root always has access; access for any other UIDs should always
	 * be controlled through packages.list. */
	if (current_fsuid() == 0) {
		return 1;
	}

	/* If asking to write, verify that caller either owns the
	 * parent or holds sdcard_rw. */
	if (w_ok) {
		if (parent_node &&
			(current_fsuid() == SDCARDFS_I(parent_node)->d_uid)) {
			return 1;
		}
		return has_rw;
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

static int insert_str_to_int(struct packagelist_data *pkgl_dat, void *key, int value) {
	struct hashtable_entry *hash_cur;
	struct hashtable_entry *new_entry;
	struct hlist_node *h_n;
	unsigned int hash = str_hash(key);

	//printk(KERN_INFO "sdcardfs: %s: %s: %d, %u\n", __func__, (char *)key, value, hash);
	hash_for_each_possible(pkgl_dat->package_to_appid, hash_cur, hlist, hash, h_n) {
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

static void remove_str_to_int(struct hashtable_entry *h_entry) {
	//printk(KERN_INFO "sdcardfs: %s: %s: %d\n", __func__, (char *)h_entry->key, h_entry->value);
	kfree(h_entry->key);
	kmem_cache_free(hashtable_entry_cachep, h_entry);
}

static int insert_int_to_null(struct packagelist_data *pkgl_dat, void *key, int value) {
	struct hashtable_entry *hash_cur;
	struct hashtable_entry *new_entry;
	struct hlist_node *h_n;

	//printk(KERN_INFO "sdcardfs: %s: %d: %d\n", __func__, (int)key, value);
	hash_for_each_possible(pkgl_dat->appid_with_rw,	hash_cur, hlist,
					(unsigned int)key, h_n) {
		if (key == hash_cur->key) {
			hash_cur->value = value;
			return 0;
		}
	}
	new_entry = kmem_cache_alloc(hashtable_entry_cachep, GFP_KERNEL);
	if (!new_entry)
		return -ENOMEM;
	new_entry->key = key;
	new_entry->value = value;
	hash_add(pkgl_dat->appid_with_rw, &new_entry->hlist,
			(unsigned int)new_entry->key);
	return 0;
}

static void remove_int_to_null(struct hashtable_entry *h_entry) {
	//printk(KERN_INFO "sdcardfs: %s: %d: %d\n", __func__, (int)h_entry->key, h_entry->value);
	kmem_cache_free(hashtable_entry_cachep, h_entry);
}

static void remove_all_hashentrys(struct packagelist_data *pkgl_dat)
{
	struct hashtable_entry *hash_cur;
	struct hlist_node *h_n;
	struct hlist_node *h_t;
	int i;

	hash_for_each_safe(pkgl_dat->package_to_appid, i, h_t, hash_cur, hlist, h_n)
		remove_str_to_int(hash_cur);
	hash_for_each_safe(pkgl_dat->appid_with_rw, i, h_t, hash_cur, hlist, h_n)
                remove_int_to_null(hash_cur);

	hash_init(pkgl_dat->package_to_appid);
	hash_init(pkgl_dat->appid_with_rw);
}

static int read_package_list(struct packagelist_data *pkgl_dat) {
	int ret;
	int fd;
	int read_amount;

	printk(KERN_INFO "sdcardfs: read_package_list\n");

	mutex_lock(&pkgl_dat->hashtable_lock);

	remove_all_hashentrys(pkgl_dat);

	fd = sys_open(kpackageslist_file, O_RDONLY, 0);
	if (fd < 0) {
		printk(KERN_ERR "sdcardfs: failed to open package list\n");
		mutex_unlock(&pkgl_dat->hashtable_lock);
		return fd;
	}

	while ((read_amount = sys_read(fd, pkgl_dat->read_buf,
					sizeof(pkgl_dat->read_buf))) > 0) {
		int appid;
		char *token;
		int one_line_len = 0;
		int additional_read;
		unsigned long ret_gid;

		while (one_line_len < read_amount) {
			if (pkgl_dat->read_buf[one_line_len] == '\n') {
				one_line_len++;
				break;
			}
			one_line_len++;
		}
		additional_read = read_amount - one_line_len;
		if (additional_read > 0)
			sys_lseek(fd, -additional_read, SEEK_CUR);

		if (sscanf(pkgl_dat->read_buf, "%s %d %*d %*s %*s %s",
				pkgl_dat->app_name_buf, &appid,
				pkgl_dat->gids_buf) == 3) {
			ret = insert_str_to_int(pkgl_dat, pkgl_dat->app_name_buf, appid);
			if (ret) {
				sys_close(fd);
				mutex_unlock(&pkgl_dat->hashtable_lock);
				return ret;
			}

			token = strtok_r(pkgl_dat->gids_buf, ",", &pkgl_dat->strtok_last);
			while (token != NULL) {
				if (!kstrtoul(token, 10, &ret_gid) &&
						(ret_gid == pkgl_dat->write_gid)) {
					ret = insert_int_to_null(pkgl_dat, (void *)appid, 1);
					if (ret) {
						sys_close(fd);
						mutex_unlock(&pkgl_dat->hashtable_lock);
						return ret;
					}
					break;
				}
				token = strtok_r(NULL, ",", &pkgl_dat->strtok_last);
			}
		}
	}

	sys_close(fd);
	mutex_unlock(&pkgl_dat->hashtable_lock);
	return 0;
}

static int packagelist_reader(void *thread_data)
{
	struct packagelist_data *pkgl_dat = (struct packagelist_data *)thread_data;
	struct inotify_event *event;
	bool active = false;
	int event_pos;
	int event_size;
	int res = 0;
	int nfd;

	allow_signal(SIGINT);

	nfd = sys_inotify_init();
	if (nfd < 0) {
		printk(KERN_ERR "sdcardfs: inotify_init failed: %d\n", nfd);
		return nfd;
	}

	while (!kthread_should_stop()) {
		if (signal_pending(current)) {
			ssleep(1);
			continue;
		}

		if (!active) {
			res = sys_inotify_add_watch(nfd, kpackageslist_file, IN_DELETE_SELF);
			if (res < 0) {
				if (res == -ENOENT || res == -EACCES) {
				/* Framework may not have created yet, sleep and retry */
					printk(KERN_ERR "sdcardfs: missing packages.list; retrying\n");
					ssleep(2);
					printk(KERN_ERR "sdcardfs: missing packages.list_end; retrying\n");
					continue;
				} else {
					printk(KERN_ERR "sdcardfs: inotify_add_watch failed: %d\n", res);
					goto interruptable_sleep;
				}
			}
			/* Watch above will tell us about any future changes, so
			 * read the current state. */
			res = read_package_list(pkgl_dat);
			if (res) {
				printk(KERN_ERR "sdcardfs: read_package_list failed: %d\n", res);
				goto interruptable_sleep;
			}
			active = true;
		}

		event_pos = 0;
		res = sys_read(nfd, pkgl_dat->event_buf, sizeof(pkgl_dat->event_buf));
		if (res < (int) sizeof(*event)) {
			if (res == -EINTR)
				continue;
			printk(KERN_ERR "sdcardfs: failed to read inotify event: %d\n", res);
			goto interruptable_sleep;
		}

		while (res >= (int) sizeof(*event)) {
			event = (struct inotify_event *) (pkgl_dat->event_buf + event_pos);

			printk(KERN_INFO "sdcardfs: inotify event: %08x\n", event->mask);
			if ((event->mask & IN_IGNORED) == IN_IGNORED) {
				/* Previously watched file was deleted, probably due to move
				 * that swapped in new data; re-arm the watch and read. */
				active = false;
			}

			event_size = sizeof(*event) + event->len;
			res -= event_size;
			event_pos += event_size;
		}
		continue;

interruptable_sleep:
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
	flush_signals(current);
	sys_close(nfd);
	return res;
}

void * packagelist_create(gid_t write_gid)
{
	struct packagelist_data *pkgl_dat;
        struct task_struct *packagelist_thread;

	pkgl_dat = kmalloc(sizeof(*pkgl_dat), GFP_KERNEL | __GFP_ZERO);
	if (!pkgl_dat) {
                printk(KERN_ERR "sdcardfs: creating kthread failed\n");
		return ERR_PTR(-ENOMEM);
	}

	mutex_init(&pkgl_dat->hashtable_lock);
	hash_init(pkgl_dat->package_to_appid);
	hash_init(pkgl_dat->appid_with_rw);
	pkgl_dat->write_gid = write_gid;

        packagelist_thread = kthread_run(packagelist_reader, (void *)pkgl_dat, "pkgld");
        if (IS_ERR(packagelist_thread)) {
                printk(KERN_ERR "sdcardfs: creating kthread failed\n");
		kfree(pkgl_dat);
		return packagelist_thread;
        }
	pkgl_dat->thread_id = packagelist_thread;

	printk(KERN_INFO "sdcardfs: created packagelist pkgld/%d\n",
				(int)pkgl_dat->thread_id->pid);

	return (void *)pkgl_dat;
}

void packagelist_destroy(void *pkgl_id)
{
	struct packagelist_data *pkgl_dat = (struct packagelist_data *)pkgl_id;
	pid_t pkgl_pid = pkgl_dat->thread_id->pid;

	force_sig_info(SIGINT, SEND_SIG_PRIV, pkgl_dat->thread_id);
	kthread_stop(pkgl_dat->thread_id);
	remove_all_hashentrys(pkgl_dat);
	printk(KERN_INFO "sdcardfs: destroyed packagelist pkgld/%d\n", (int)pkgl_pid);
	kfree(pkgl_dat);
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

        return 0;
}

void packagelist_exit(void)
{
	if (hashtable_entry_cachep)
		kmem_cache_destroy(hashtable_entry_cachep);
}


