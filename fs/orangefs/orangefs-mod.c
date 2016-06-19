/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add proc file handler for pvfs2 client
 * parameters, Copyright Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-debugfs.h"
#include "orangefs-sysfs.h"

/* ORANGEFS_VERSION is a ./configure define */
#ifndef ORANGEFS_VERSION
#define ORANGEFS_VERSION "upstream"
#endif

/*
 * global variables declared here
 */

/* array of client debug keyword/mask values */
struct client_debug_mask *cdm_array;
int cdm_element_count;

char kernel_debug_string[ORANGEFS_MAX_DEBUG_STRING_LEN] = "none";
char client_debug_string[ORANGEFS_MAX_DEBUG_STRING_LEN];
char client_debug_array_string[ORANGEFS_MAX_DEBUG_STRING_LEN];

char *debug_help_string;
int help_string_initialized;
struct dentry *help_file_dentry;
struct dentry *client_debug_dentry;
struct dentry *debug_dir;
int client_verbose_index;
int client_all_index;
struct orangefs_stats g_orangefs_stats;

/* the size of the hash tables for ops in progress */
int hash_table_size = 509;

static ulong module_parm_debug_mask;
__u64 gossip_debug_mask;
struct client_debug_mask client_debug_mask = { NULL, 0, 0 };
unsigned int kernel_mask_set_mod_init; /* implicitly false */
int op_timeout_secs = ORANGEFS_DEFAULT_OP_TIMEOUT_SECS;
int slot_timeout_secs = ORANGEFS_DEFAULT_SLOT_TIMEOUT_SECS;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ORANGEFS Development Team");
MODULE_DESCRIPTION("The Linux Kernel VFS interface to ORANGEFS");
MODULE_PARM_DESC(module_parm_debug_mask, "debugging level (see orangefs-debug.h for values)");
MODULE_PARM_DESC(op_timeout_secs, "Operation timeout in seconds");
MODULE_PARM_DESC(slot_timeout_secs, "Slot timeout in seconds");
MODULE_PARM_DESC(hash_table_size,
		 "size of hash table for operations in progress");

static struct file_system_type orangefs_fs_type = {
	.name = "pvfs2",
	.mount = orangefs_mount,
	.kill_sb = orangefs_kill_sb,
	.owner = THIS_MODULE,
};

module_param(hash_table_size, int, 0);
module_param(module_parm_debug_mask, ulong, 0644);
module_param(op_timeout_secs, int, 0);
module_param(slot_timeout_secs, int, 0);

/* synchronizes the request device file */
DEFINE_MUTEX(devreq_mutex);

/*
 * Blocks non-priority requests from being queued for servicing.  This
 * could be used for protecting the request list data structure, but
 * for now it's only being used to stall the op addition to the request
 * list
 */
DEFINE_MUTEX(request_mutex);

/* hash table for storing operations waiting for matching downcall */
struct list_head *htable_ops_in_progress;
DEFINE_SPINLOCK(htable_ops_in_progress_lock);

/* list for queueing upcall operations */
LIST_HEAD(orangefs_request_list);

/* used to protect the above orangefs_request_list */
DEFINE_SPINLOCK(orangefs_request_list_lock);

/* used for incoming request notification */
DECLARE_WAIT_QUEUE_HEAD(orangefs_request_list_waitq);

static int __init orangefs_init(void)
{
	int ret = -1;
	__u32 i = 0;

	/* convert input debug mask to a 64-bit unsigned integer */
	gossip_debug_mask = (unsigned long long) module_parm_debug_mask;

	/*
	 * set the kernel's gossip debug string; invalid mask values will
	 * be ignored.
	 */
	debug_mask_to_string(&gossip_debug_mask, 0);

	/* remove any invalid values from the mask */
	debug_string_to_mask(kernel_debug_string, &gossip_debug_mask, 0);

	/*
	 * if the mask has a non-zero value, then indicate that the mask
	 * was set when the kernel module was loaded.  The orangefs dev ioctl
	 * command will look at this boolean to determine if the kernel's
	 * debug mask should be overwritten when the client-core is started.
	 */
	if (gossip_debug_mask != 0)
		kernel_mask_set_mod_init = true;

	pr_info("%s: called with debug mask: :%s: :%llx:\n",
		__func__,
		kernel_debug_string,
		(unsigned long long)gossip_debug_mask);

	ret = bdi_init(&orangefs_backing_dev_info);

	if (ret)
		return ret;

	if (op_timeout_secs < 0)
		op_timeout_secs = 0;

	if (slot_timeout_secs < 0)
		slot_timeout_secs = 0;

	/* initialize global book keeping data structures */
	ret = op_cache_initialize();
	if (ret < 0)
		goto err;

	ret = orangefs_inode_cache_initialize();
	if (ret < 0)
		goto cleanup_op;

	htable_ops_in_progress =
	    kcalloc(hash_table_size, sizeof(struct list_head), GFP_KERNEL);
	if (!htable_ops_in_progress) {
		gossip_err("Failed to initialize op hashtable");
		ret = -ENOMEM;
		goto cleanup_inode;
	}

	/* initialize a doubly linked at each hash table index */
	for (i = 0; i < hash_table_size; i++)
		INIT_LIST_HEAD(&htable_ops_in_progress[i]);

	ret = fsid_key_table_initialize();
	if (ret < 0)
		goto cleanup_progress_table;

	/*
	 * Build the contents of /sys/kernel/debug/orangefs/debug-help
	 * from the keywords in the kernel keyword/mask array.
	 *
	 * The keywords in the client keyword/mask array are
	 * unknown at boot time.
	 *
	 * orangefs_prepare_debugfs_help_string will be used again
	 * later to rebuild the debug-help file after the client starts
	 * and passes along the needed info. The argument signifies
	 * which time orangefs_prepare_debugfs_help_string is being
	 * called.
	 */
	ret = orangefs_prepare_debugfs_help_string(1);
	if (ret)
		goto cleanup_key_table;

	ret = orangefs_debugfs_init();
	if (ret)
		goto debugfs_init_failed;

	ret = orangefs_kernel_debug_init();
	if (ret)
		goto kernel_debug_init_failed;

	ret = orangefs_sysfs_init();
	if (ret)
		goto sysfs_init_failed;

	/* Initialize the orangefsdev subsystem. */
	ret = orangefs_dev_init();
	if (ret < 0) {
		gossip_err("%s: could not initialize device subsystem %d!\n",
			   __func__,
			   ret);
		goto cleanup_device;
	}

	ret = register_filesystem(&orangefs_fs_type);
	if (ret == 0) {
		pr_info("orangefs: module version %s loaded\n", ORANGEFS_VERSION);
		ret = 0;
		goto out;
	}

	orangefs_sysfs_exit();

cleanup_device:
	orangefs_dev_cleanup();

sysfs_init_failed:

kernel_debug_init_failed:

debugfs_init_failed:
	orangefs_debugfs_cleanup();

cleanup_key_table:
	fsid_key_table_finalize();

cleanup_progress_table:
	kfree(htable_ops_in_progress);

cleanup_inode:
	orangefs_inode_cache_finalize();

cleanup_op:
	op_cache_finalize();

err:
	bdi_destroy(&orangefs_backing_dev_info);

out:
	return ret;
}

static void __exit orangefs_exit(void)
{
	int i = 0;
	gossip_debug(GOSSIP_INIT_DEBUG, "orangefs: orangefs_exit called\n");

	unregister_filesystem(&orangefs_fs_type);
	orangefs_debugfs_cleanup();
	orangefs_sysfs_exit();
	fsid_key_table_finalize();
	orangefs_dev_cleanup();
	BUG_ON(!list_empty(&orangefs_request_list));
	for (i = 0; i < hash_table_size; i++)
		BUG_ON(!list_empty(&htable_ops_in_progress[i]));

	orangefs_inode_cache_finalize();
	op_cache_finalize();

	kfree(htable_ops_in_progress);

	bdi_destroy(&orangefs_backing_dev_info);

	pr_info("orangefs: module version %s unloaded\n", ORANGEFS_VERSION);
}

/*
 * What we do in this function is to walk the list of operations
 * that are in progress in the hash table and mark them as purged as well.
 */
void purge_inprogress_ops(void)
{
	int i;

	for (i = 0; i < hash_table_size; i++) {
		struct orangefs_kernel_op_s *op;
		struct orangefs_kernel_op_s *next;

		spin_lock(&htable_ops_in_progress_lock);
		list_for_each_entry_safe(op,
					 next,
					 &htable_ops_in_progress[i],
					 list) {
			set_op_state_purged(op);
			gossip_debug(GOSSIP_DEV_DEBUG,
				     "%s: op:%s: op_state:%d: process:%s:\n",
				     __func__,
				     get_opname_string(op),
				     op->op_state,
				     current->comm);
		}
		spin_unlock(&htable_ops_in_progress_lock);
	}
}

module_init(orangefs_init);
module_exit(orangefs_exit);
