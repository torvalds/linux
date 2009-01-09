/*
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
 *
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/dma-debug.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>

#define HASH_SIZE       1024ULL
#define HASH_FN_SHIFT   13
#define HASH_FN_MASK    (HASH_SIZE - 1)

enum {
	dma_debug_single,
	dma_debug_page,
	dma_debug_sg,
	dma_debug_coherent,
};

struct dma_debug_entry {
	struct list_head list;
	struct device    *dev;
	int              type;
	phys_addr_t      paddr;
	u64              dev_addr;
	u64              size;
	int              direction;
	int		 sg_call_ents;
	int		 sg_mapped_ents;
};

struct hash_bucket {
	struct list_head list;
	spinlock_t lock;
} __cacheline_aligned_in_smp;

/* Hash list to save the allocated dma addresses */
static struct hash_bucket dma_entry_hash[HASH_SIZE];
/* List of pre-allocated dma_debug_entry's */
static LIST_HEAD(free_entries);
/* Lock for the list above */
static DEFINE_SPINLOCK(free_entries_lock);

/* Global disable flag - will be set in case of an error */
static bool global_disable __read_mostly;

/* Global error count */
static u32 error_count;

/* Global error show enable*/
static u32 show_all_errors __read_mostly;
/* Number of errors to show */
static u32 show_num_errors = 1;

static u32 num_free_entries;
static u32 min_free_entries;

/* number of preallocated entries requested by kernel cmdline */
static u32 req_entries;

/* debugfs dentry's for the stuff above */
static struct dentry *dma_debug_dent        __read_mostly;
static struct dentry *global_disable_dent   __read_mostly;
static struct dentry *error_count_dent      __read_mostly;
static struct dentry *show_all_errors_dent  __read_mostly;
static struct dentry *show_num_errors_dent  __read_mostly;
static struct dentry *num_free_entries_dent __read_mostly;
static struct dentry *min_free_entries_dent __read_mostly;

/*
 * Hash related functions
 *
 * Every DMA-API request is saved into a struct dma_debug_entry. To
 * have quick access to these structs they are stored into a hash.
 */
static int hash_fn(struct dma_debug_entry *entry)
{
	/*
	 * Hash function is based on the dma address.
	 * We use bits 20-27 here as the index into the hash
	 */
	return (entry->dev_addr >> HASH_FN_SHIFT) & HASH_FN_MASK;
}

/*
 * Request exclusive access to a hash bucket for a given dma_debug_entry.
 */
static struct hash_bucket *get_hash_bucket(struct dma_debug_entry *entry,
					   unsigned long *flags)
{
	int idx = hash_fn(entry);
	unsigned long __flags;

	spin_lock_irqsave(&dma_entry_hash[idx].lock, __flags);
	*flags = __flags;
	return &dma_entry_hash[idx];
}

/*
 * Give up exclusive access to the hash bucket
 */
static void put_hash_bucket(struct hash_bucket *bucket,
			    unsigned long *flags)
{
	unsigned long __flags = *flags;

	spin_unlock_irqrestore(&bucket->lock, __flags);
}

/*
 * Search a given entry in the hash bucket list
 */
static struct dma_debug_entry *hash_bucket_find(struct hash_bucket *bucket,
						struct dma_debug_entry *ref)
{
	struct dma_debug_entry *entry;

	list_for_each_entry(entry, &bucket->list, list) {
		if ((entry->dev_addr == ref->dev_addr) &&
		    (entry->dev == ref->dev))
			return entry;
	}

	return NULL;
}

/*
 * Add an entry to a hash bucket
 */
static void hash_bucket_add(struct hash_bucket *bucket,
			    struct dma_debug_entry *entry)
{
	list_add_tail(&entry->list, &bucket->list);
}

/*
 * Remove entry from a hash bucket list
 */
static void hash_bucket_del(struct dma_debug_entry *entry)
{
	list_del(&entry->list);
}

/*
 * Wrapper function for adding an entry to the hash.
 * This function takes care of locking itself.
 */
static void add_dma_entry(struct dma_debug_entry *entry)
{
	struct hash_bucket *bucket;
	unsigned long flags;

	bucket = get_hash_bucket(entry, &flags);
	hash_bucket_add(bucket, entry);
	put_hash_bucket(bucket, &flags);
}

/* struct dma_entry allocator
 *
 * The next two functions implement the allocator for
 * struct dma_debug_entries.
 */
static struct dma_debug_entry *dma_entry_alloc(void)
{
	struct dma_debug_entry *entry = NULL;
	unsigned long flags;

	spin_lock_irqsave(&free_entries_lock, flags);

	if (list_empty(&free_entries)) {
		printk(KERN_ERR "DMA-API: debugging out of memory "
				"- disabling\n");
		global_disable = true;
		goto out;
	}

	entry = list_entry(free_entries.next, struct dma_debug_entry, list);
	list_del(&entry->list);
	memset(entry, 0, sizeof(*entry));

	num_free_entries -= 1;
	if (num_free_entries < min_free_entries)
		min_free_entries = num_free_entries;

out:
	spin_unlock_irqrestore(&free_entries_lock, flags);

	return entry;
}

static void dma_entry_free(struct dma_debug_entry *entry)
{
	unsigned long flags;

	/*
	 * add to beginning of the list - this way the entries are
	 * more likely cache hot when they are reallocated.
	 */
	spin_lock_irqsave(&free_entries_lock, flags);
	list_add(&entry->list, &free_entries);
	num_free_entries += 1;
	spin_unlock_irqrestore(&free_entries_lock, flags);
}

/*
 * DMA-API debugging init code
 *
 * The init code does two things:
 *   1. Initialize core data structures
 *   2. Preallocate a given number of dma_debug_entry structs
 */

static int prealloc_memory(u32 num_entries)
{
	struct dma_debug_entry *entry, *next_entry;
	int i;

	for (i = 0; i < num_entries; ++i) {
		entry = kzalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			goto out_err;

		list_add_tail(&entry->list, &free_entries);
	}

	num_free_entries = num_entries;
	min_free_entries = num_entries;

	printk(KERN_INFO "DMA-API: preallocated %d debug entries\n",
			num_entries);

	return 0;

out_err:

	list_for_each_entry_safe(entry, next_entry, &free_entries, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	return -ENOMEM;
}

static int dma_debug_fs_init(void)
{
	dma_debug_dent = debugfs_create_dir("dma-api", NULL);
	if (!dma_debug_dent) {
		printk(KERN_ERR "DMA-API: can not create debugfs directory\n");
		return -ENOMEM;
	}

	global_disable_dent = debugfs_create_bool("disabled", 0444,
			dma_debug_dent,
			(u32 *)&global_disable);
	if (!global_disable_dent)
		goto out_err;

	error_count_dent = debugfs_create_u32("error_count", 0444,
			dma_debug_dent, &error_count);
	if (!error_count_dent)
		goto out_err;

	show_all_errors_dent = debugfs_create_u32("all_errors", 0644,
			dma_debug_dent,
			&show_all_errors);
	if (!show_all_errors_dent)
		goto out_err;

	show_num_errors_dent = debugfs_create_u32("num_errors", 0644,
			dma_debug_dent,
			&show_num_errors);
	if (!show_num_errors_dent)
		goto out_err;

	num_free_entries_dent = debugfs_create_u32("num_free_entries", 0444,
			dma_debug_dent,
			&num_free_entries);
	if (!num_free_entries_dent)
		goto out_err;

	min_free_entries_dent = debugfs_create_u32("min_free_entries", 0444,
			dma_debug_dent,
			&min_free_entries);
	if (!min_free_entries_dent)
		goto out_err;

	return 0;

out_err:
	debugfs_remove_recursive(dma_debug_dent);

	return -ENOMEM;
}


/*
 * Let the architectures decide how many entries should be preallocated.
 */
void dma_debug_init(u32 num_entries)
{
	int i;

	if (global_disable)
		return;

	for (i = 0; i < HASH_SIZE; ++i) {
		INIT_LIST_HEAD(&dma_entry_hash[i].list);
		dma_entry_hash[i].lock = SPIN_LOCK_UNLOCKED;
	}

	if (dma_debug_fs_init() != 0) {
		printk(KERN_ERR "DMA-API: error creating debugfs entries "
				"- disabling\n");
		global_disable = true;

		return;
	}

	if (req_entries)
		num_entries = req_entries;

	if (prealloc_memory(num_entries) != 0) {
		printk(KERN_ERR "DMA-API: debugging out of memory error "
				"- disabled\n");
		global_disable = true;

		return;
	}

	printk(KERN_INFO "DMA-API: debugging enabled by kernel config\n");
}

static __init int dma_debug_cmdline(char *str)
{
	if (!str)
		return -EINVAL;

	if (strncmp(str, "off", 3) == 0) {
		printk(KERN_INFO "DMA-API: debugging disabled on kernel "
				 "command line\n");
		global_disable = true;
	}

	return 0;
}

static __init int dma_debug_entries_cmdline(char *str)
{
	int res;

	if (!str)
		return -EINVAL;

	res = get_option(&str, &req_entries);

	if (!res)
		req_entries = 0;

	return 0;
}

__setup("dma_debug=", dma_debug_cmdline);
__setup("dma_debug_entries=", dma_debug_entries_cmdline);

