/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * Changes by Acxiom Corporation to add protocol version to kernel
 * communication, Copyright Acxiom Corporation, 2005.
 *
 * See COPYING in top-level directory.
 */

#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-dev-proto.h"
#include "orangefs-bufmap.h"

#include <linux/debugfs.h>
#include <linux/slab.h>

/* this file implements the /dev/pvfs2-req device node */

static int open_access_count;

#define DUMP_DEVICE_ERROR()                                                   \
do {                                                                          \
	gossip_err("*****************************************************\n");\
	gossip_err("ORANGEFS Device Error:  You cannot open the device file ");  \
	gossip_err("\n/dev/%s more than once.  Please make sure that\nthere " \
		   "are no ", ORANGEFS_REQDEVICE_NAME);                          \
	gossip_err("instances of a program using this device\ncurrently "     \
		   "running. (You must verify this!)\n");                     \
	gossip_err("For example, you can use the lsof program as follows:\n");\
	gossip_err("'lsof | grep %s' (run this as root)\n",                   \
		   ORANGEFS_REQDEVICE_NAME);                                     \
	gossip_err("  open_access_count = %d\n", open_access_count);          \
	gossip_err("*****************************************************\n");\
} while (0)

static int hash_func(__u64 tag, int table_size)
{
	return do_div(tag, (unsigned int)table_size);
}

static void orangefs_devreq_add_op(struct orangefs_kernel_op_s *op)
{
	int index = hash_func(op->tag, hash_table_size);

	list_add_tail(&op->list, &htable_ops_in_progress[index]);
}

/*
 * find the op with this tag and remove it from the in progress
 * hash table.
 */
static struct orangefs_kernel_op_s *orangefs_devreq_remove_op(__u64 tag)
{
	struct orangefs_kernel_op_s *op, *next;
	int index;

	index = hash_func(tag, hash_table_size);

	spin_lock(&htable_ops_in_progress_lock);
	list_for_each_entry_safe(op,
				 next,
				 &htable_ops_in_progress[index],
				 list) {
		if (op->tag == tag && !op_state_purged(op) &&
		    !op_state_given_up(op)) {
			list_del_init(&op->list);
			spin_unlock(&htable_ops_in_progress_lock);
			return op;
		}
	}

	spin_unlock(&htable_ops_in_progress_lock);
	return NULL;
}

/* Returns whether any FS are still pending remounted */
static int mark_all_pending_mounts(void)
{
	int unmounted = 1;
	struct orangefs_sb_info_s *orangefs_sb = NULL;

	spin_lock(&orangefs_superblocks_lock);
	list_for_each_entry(orangefs_sb, &orangefs_superblocks, list) {
		/* All of these file system require a remount */
		orangefs_sb->mount_pending = 1;
		unmounted = 0;
	}
	spin_unlock(&orangefs_superblocks_lock);
	return unmounted;
}

/*
 * Determine if a given file system needs to be remounted or not
 *  Returns -1 on error
 *           0 if already mounted
 *           1 if needs remount
 */
static int fs_mount_pending(__s32 fsid)
{
	int mount_pending = -1;
	struct orangefs_sb_info_s *orangefs_sb = NULL;

	spin_lock(&orangefs_superblocks_lock);
	list_for_each_entry(orangefs_sb, &orangefs_superblocks, list) {
		if (orangefs_sb->fs_id == fsid) {
			mount_pending = orangefs_sb->mount_pending;
			break;
		}
	}
	spin_unlock(&orangefs_superblocks_lock);
	return mount_pending;
}

static int orangefs_devreq_open(struct inode *inode, struct file *file)
{
	int ret = -EINVAL;

	if (!(file->f_flags & O_NONBLOCK)) {
		gossip_err("%s: device cannot be opened in blocking mode\n",
			   __func__);
		goto out;
	}
	ret = -EACCES;
	gossip_debug(GOSSIP_DEV_DEBUG, "client-core: opening device\n");
	mutex_lock(&devreq_mutex);

	if (open_access_count == 0) {
		open_access_count = 1;
		ret = 0;
	} else {
		DUMP_DEVICE_ERROR();
	}
	mutex_unlock(&devreq_mutex);

out:

	gossip_debug(GOSSIP_DEV_DEBUG,
		     "pvfs2-client-core: open device complete (ret = %d)\n",
		     ret);
	return ret;
}

/* Function for read() callers into the device */
static ssize_t orangefs_devreq_read(struct file *file,
				 char __user *buf,
				 size_t count, loff_t *offset)
{
	struct orangefs_kernel_op_s *op, *temp;
	__s32 proto_ver = ORANGEFS_KERNEL_PROTO_VERSION;
	static __s32 magic = ORANGEFS_DEVREQ_MAGIC;
	struct orangefs_kernel_op_s *cur_op = NULL;
	unsigned long ret;

	/* We do not support blocking IO. */
	if (!(file->f_flags & O_NONBLOCK)) {
		gossip_err("%s: blocking read from client-core.\n",
			   __func__);
		return -EINVAL;
	}

	/*
	 * The client will do an ioctl to find MAX_DEV_REQ_UPSIZE, then
	 * always read with that size buffer.
	 */
	if (count != MAX_DEV_REQ_UPSIZE) {
		gossip_err("orangefs: client-core tried to read wrong size\n");
		return -EINVAL;
	}

restart:
	/* Get next op (if any) from top of list. */
	spin_lock(&orangefs_request_list_lock);
	list_for_each_entry_safe(op, temp, &orangefs_request_list, list) {
		__s32 fsid;
		/* This lock is held past the end of the loop when we break. */
		spin_lock(&op->lock);
		if (unlikely(op_state_purged(op) || op_state_given_up(op))) {
			spin_unlock(&op->lock);
			continue;
		}

		fsid = fsid_of_op(op);
		if (fsid != ORANGEFS_FS_ID_NULL) {
			int ret;
			/* Skip ops whose filesystem needs to be mounted. */
			ret = fs_mount_pending(fsid);
			if (ret == 1) {
				gossip_debug(GOSSIP_DEV_DEBUG,
				    "%s: mount pending, skipping op tag "
				    "%llu %s\n",
				    __func__,
				    llu(op->tag),
				    get_opname_string(op));
				spin_unlock(&op->lock);
				continue;
			/*
			 * Skip ops whose filesystem we don't know about unless
			 * it is being mounted.
			 */
			/* XXX: is there a better way to detect this? */
			} else if (ret == -1 &&
				   !(op->upcall.type ==
					ORANGEFS_VFS_OP_FS_MOUNT ||
				     op->upcall.type ==
					ORANGEFS_VFS_OP_GETATTR)) {
				gossip_debug(GOSSIP_DEV_DEBUG,
				    "orangefs: skipping op tag %llu %s\n",
				    llu(op->tag), get_opname_string(op));
				gossip_err(
				    "orangefs: ERROR: fs_mount_pending %d\n",
				    fsid);
				spin_unlock(&op->lock);
				continue;
			}
		}
		/*
		 * Either this op does not pertain to a filesystem, is mounting
		 * a filesystem, or pertains to a mounted filesystem. Let it
		 * through.
		 */
		cur_op = op;
		break;
	}

	/*
	 * At this point we either have a valid op and can continue or have not
	 * found an op and must ask the client to try again later.
	 */
	if (!cur_op) {
		spin_unlock(&orangefs_request_list_lock);
		return -EAGAIN;
	}

	gossip_debug(GOSSIP_DEV_DEBUG, "%s: reading op tag %llu %s\n",
		     __func__,
		     llu(cur_op->tag),
		     get_opname_string(cur_op));

	/*
	 * Such an op should never be on the list in the first place. If so, we
	 * will abort.
	 */
	if (op_state_in_progress(cur_op) || op_state_serviced(cur_op)) {
		gossip_err("orangefs: ERROR: Current op already queued.\n");
		list_del_init(&cur_op->list);
		spin_unlock(&cur_op->lock);
		spin_unlock(&orangefs_request_list_lock);
		return -EAGAIN;
	}

	list_del_init(&cur_op->list);
	spin_unlock(&orangefs_request_list_lock);

	spin_unlock(&cur_op->lock);

	/* Push the upcall out. */
	ret = copy_to_user(buf, &proto_ver, sizeof(__s32));
	if (ret != 0)
		goto error;
	ret = copy_to_user(buf+sizeof(__s32), &magic, sizeof(__s32));
	if (ret != 0)
		goto error;
	ret = copy_to_user(buf+2 * sizeof(__s32), &cur_op->tag, sizeof(__u64));
	if (ret != 0)
		goto error;
	ret = copy_to_user(buf+2*sizeof(__s32)+sizeof(__u64), &cur_op->upcall,
			   sizeof(struct orangefs_upcall_s));
	if (ret != 0)
		goto error;

	spin_lock(&htable_ops_in_progress_lock);
	spin_lock(&cur_op->lock);
	if (unlikely(op_state_given_up(cur_op))) {
		spin_unlock(&cur_op->lock);
		spin_unlock(&htable_ops_in_progress_lock);
		complete(&cur_op->waitq);
		goto restart;
	}

	/*
	 * Set the operation to be in progress and move it between lists since
	 * it has been sent to the client.
	 */
	set_op_state_inprogress(cur_op);
	gossip_debug(GOSSIP_DEV_DEBUG,
		     "%s: 1 op:%s: op_state:%d: process:%s:\n",
		     __func__,
		     get_opname_string(cur_op),
		     cur_op->op_state,
		     current->comm);
	orangefs_devreq_add_op(cur_op);
	spin_unlock(&cur_op->lock);
	spin_unlock(&htable_ops_in_progress_lock);

	/* The client only asks to read one size buffer. */
	return MAX_DEV_REQ_UPSIZE;
error:
	/*
	 * We were unable to copy the op data to the client. Put the op back in
	 * list. If client has crashed, the op will be purged later when the
	 * device is released.
	 */
	gossip_err("orangefs: Failed to copy data to user space\n");
	spin_lock(&orangefs_request_list_lock);
	spin_lock(&cur_op->lock);
	if (likely(!op_state_given_up(cur_op))) {
		set_op_state_waiting(cur_op);
		gossip_debug(GOSSIP_DEV_DEBUG,
			     "%s: 2 op:%s: op_state:%d: process:%s:\n",
			     __func__,
			     get_opname_string(cur_op),
			     cur_op->op_state,
			     current->comm);
		list_add(&cur_op->list, &orangefs_request_list);
		spin_unlock(&cur_op->lock);
	} else {
		spin_unlock(&cur_op->lock);
		complete(&cur_op->waitq);
	}
	spin_unlock(&orangefs_request_list_lock);
	return -EFAULT;
}

/*
 * Function for writev() callers into the device.
 *
 * Userspace should have written:
 *  - __u32 version
 *  - __u32 magic
 *  - __u64 tag
 *  - struct orangefs_downcall_s
 *  - trailer buffer (in the case of READDIR operations)
 */
static ssize_t orangefs_devreq_write_iter(struct kiocb *iocb,
				      struct iov_iter *iter)
{
	ssize_t ret;
	struct orangefs_kernel_op_s *op = NULL;
	struct {
		__u32 version;
		__u32 magic;
		__u64 tag;
	} head;
	int total = ret = iov_iter_count(iter);
	int n;
	int downcall_size = sizeof(struct orangefs_downcall_s);
	int head_size = sizeof(head);

	gossip_debug(GOSSIP_DEV_DEBUG, "%s: total:%d: ret:%zd:\n",
		     __func__,
		     total,
		     ret);

        if (total < MAX_DEV_REQ_DOWNSIZE) {
		gossip_err("%s: total:%d: must be at least:%u:\n",
			   __func__,
			   total,
			   (unsigned int) MAX_DEV_REQ_DOWNSIZE);
		return -EFAULT;
	}
     
	n = copy_from_iter(&head, head_size, iter);
	if (n < head_size) {
		gossip_err("%s: failed to copy head.\n", __func__);
		return -EFAULT;
	}

	if (head.version < ORANGEFS_MINIMUM_USERSPACE_VERSION) {
		gossip_err("%s: userspace claims version"
			   "%d, minimum version required: %d.\n",
			   __func__,
			   head.version,
			   ORANGEFS_MINIMUM_USERSPACE_VERSION);
		return -EPROTO;
	}

	if (head.magic != ORANGEFS_DEVREQ_MAGIC) {
		gossip_err("Error: Device magic number does not match.\n");
		return -EPROTO;
	}

	/* remove the op from the in progress hash table */
	op = orangefs_devreq_remove_op(head.tag);
	if (!op) {
		gossip_err("WARNING: No one's waiting for tag %llu\n",
			   llu(head.tag));
		return ret;
	}

	n = copy_from_iter(&op->downcall, downcall_size, iter);
	if (n != downcall_size) {
		gossip_err("%s: failed to copy downcall.\n", __func__);
		goto Efault;
	}

	if (op->downcall.status)
		goto wakeup;

	/*
	 * We've successfully peeled off the head and the downcall. 
	 * Something has gone awry if total doesn't equal the
	 * sum of head_size, downcall_size and trailer_size.
	 */
	if ((head_size + downcall_size + op->downcall.trailer_size) != total) {
		gossip_err("%s: funky write, head_size:%d"
			   ": downcall_size:%d: trailer_size:%lld"
			   ": total size:%d:\n",
			   __func__,
			   head_size,
			   downcall_size,
			   op->downcall.trailer_size,
			   total);
		goto Efault;
	}

	/* Only READDIR operations should have trailers. */
	if ((op->downcall.type != ORANGEFS_VFS_OP_READDIR) &&
	    (op->downcall.trailer_size != 0)) {
		gossip_err("%s: %x operation with trailer.",
			   __func__,
			   op->downcall.type);
		goto Efault;
	}

	/* READDIR operations should always have trailers. */
	if ((op->downcall.type == ORANGEFS_VFS_OP_READDIR) &&
	    (op->downcall.trailer_size == 0)) {
		gossip_err("%s: %x operation with no trailer.",
			   __func__,
			   op->downcall.type);
		goto Efault;
	}

	if (op->downcall.type != ORANGEFS_VFS_OP_READDIR)
		goto wakeup;

	op->downcall.trailer_buf =
		vmalloc(op->downcall.trailer_size);
	if (op->downcall.trailer_buf == NULL) {
		gossip_err("%s: failed trailer vmalloc.\n",
			   __func__);
		goto Enomem;
	}
	memset(op->downcall.trailer_buf, 0, op->downcall.trailer_size);
	n = copy_from_iter(op->downcall.trailer_buf,
			   op->downcall.trailer_size,
			   iter);
	if (n != op->downcall.trailer_size) {
		gossip_err("%s: failed to copy trailer.\n", __func__);
		vfree(op->downcall.trailer_buf);
		goto Efault;
	}

wakeup:
	/*
	 * Return to vfs waitqueue, and back to service_operation
	 * through wait_for_matching_downcall. 
	 */
	spin_lock(&op->lock);
	if (unlikely(op_is_cancel(op))) {
		spin_unlock(&op->lock);
		put_cancel(op);
	} else if (unlikely(op_state_given_up(op))) {
		spin_unlock(&op->lock);
		complete(&op->waitq);
	} else {
		set_op_state_serviced(op);
		gossip_debug(GOSSIP_DEV_DEBUG,
			     "%s: op:%s: op_state:%d: process:%s:\n",
			     __func__,
			     get_opname_string(op),
			     op->op_state,
			     current->comm);
		spin_unlock(&op->lock);
	}
	return ret;

Efault:
	op->downcall.status = -(ORANGEFS_ERROR_BIT | 9);
	ret = -EFAULT;
	goto wakeup;

Enomem:
	op->downcall.status = -(ORANGEFS_ERROR_BIT | 8);
	ret = -ENOMEM;
	goto wakeup;
}

/*
 * NOTE: gets called when the last reference to this device is dropped.
 * Using the open_access_count variable, we enforce a reference count
 * on this file so that it can be opened by only one process at a time.
 * the devreq_mutex is used to make sure all i/o has completed
 * before we call orangefs_bufmap_finalize, and similar such tricky
 * situations
 */
static int orangefs_devreq_release(struct inode *inode, struct file *file)
{
	int unmounted = 0;

	gossip_debug(GOSSIP_DEV_DEBUG,
		     "%s:pvfs2-client-core: exiting, closing device\n",
		     __func__);

	mutex_lock(&devreq_mutex);
	orangefs_bufmap_finalize();

	open_access_count = -1;

	unmounted = mark_all_pending_mounts();
	gossip_debug(GOSSIP_DEV_DEBUG, "ORANGEFS Device Close: Filesystem(s) %s\n",
		     (unmounted ? "UNMOUNTED" : "MOUNTED"));

	purge_waiting_ops();
	purge_inprogress_ops();

	orangefs_bufmap_run_down();

	gossip_debug(GOSSIP_DEV_DEBUG,
		     "pvfs2-client-core: device close complete\n");
	open_access_count = 0;
	mutex_unlock(&devreq_mutex);
	return 0;
}

int is_daemon_in_service(void)
{
	int in_service;

	/*
	 * What this function does is checks if client-core is alive
	 * based on the access count we maintain on the device.
	 */
	mutex_lock(&devreq_mutex);
	in_service = open_access_count == 1 ? 0 : -EIO;
	mutex_unlock(&devreq_mutex);
	return in_service;
}

bool __is_daemon_in_service(void)
{
	return open_access_count == 1;
}

static inline long check_ioctl_command(unsigned int command)
{
	/* Check for valid ioctl codes */
	if (_IOC_TYPE(command) != ORANGEFS_DEV_MAGIC) {
		gossip_err("device ioctl magic numbers don't match! Did you rebuild pvfs2-client-core/libpvfs2? [cmd %x, magic %x != %x]\n",
			command,
			_IOC_TYPE(command),
			ORANGEFS_DEV_MAGIC);
		return -EINVAL;
	}
	/* and valid ioctl commands */
	if (_IOC_NR(command) >= ORANGEFS_DEV_MAXNR || _IOC_NR(command) <= 0) {
		gossip_err("Invalid ioctl command number [%d >= %d]\n",
			   _IOC_NR(command), ORANGEFS_DEV_MAXNR);
		return -ENOIOCTLCMD;
	}
	return 0;
}

static long dispatch_ioctl_command(unsigned int command, unsigned long arg)
{
	static __s32 magic = ORANGEFS_DEVREQ_MAGIC;
	static __s32 max_up_size = MAX_DEV_REQ_UPSIZE;
	static __s32 max_down_size = MAX_DEV_REQ_DOWNSIZE;
	struct ORANGEFS_dev_map_desc user_desc;
	int ret = 0;
	struct dev_mask_info_s mask_info = { 0 };
	struct dev_mask2_info_s mask2_info = { 0, 0 };
	int upstream_kmod = 1;
	struct orangefs_sb_info_s *orangefs_sb;

	/* mtmoore: add locking here */

	switch (command) {
	case ORANGEFS_DEV_GET_MAGIC:
		return ((put_user(magic, (__s32 __user *) arg) == -EFAULT) ?
			-EIO :
			0);
	case ORANGEFS_DEV_GET_MAX_UPSIZE:
		return ((put_user(max_up_size,
				  (__s32 __user *) arg) == -EFAULT) ?
					-EIO :
					0);
	case ORANGEFS_DEV_GET_MAX_DOWNSIZE:
		return ((put_user(max_down_size,
				  (__s32 __user *) arg) == -EFAULT) ?
					-EIO :
					0);
	case ORANGEFS_DEV_MAP:
		ret = copy_from_user(&user_desc,
				     (struct ORANGEFS_dev_map_desc __user *)
				     arg,
				     sizeof(struct ORANGEFS_dev_map_desc));
		/* WTF -EIO and not -EFAULT? */
		return ret ? -EIO : orangefs_bufmap_initialize(&user_desc);
	case ORANGEFS_DEV_REMOUNT_ALL:
		gossip_debug(GOSSIP_DEV_DEBUG,
			     "%s: got ORANGEFS_DEV_REMOUNT_ALL\n",
			     __func__);

		/*
		 * remount all mounted orangefs volumes to regain the lost
		 * dynamic mount tables (if any) -- NOTE: this is done
		 * without keeping the superblock list locked due to the
		 * upcall/downcall waiting.  also, the request mutex is
		 * used to ensure that no operations will be serviced until
		 * all of the remounts are serviced (to avoid ops between
		 * mounts to fail)
		 */
		ret = mutex_lock_interruptible(&request_mutex);
		if (ret < 0)
			return ret;
		gossip_debug(GOSSIP_DEV_DEBUG,
			     "%s: priority remount in progress\n",
			     __func__);
		spin_lock(&orangefs_superblocks_lock);
		list_for_each_entry(orangefs_sb, &orangefs_superblocks, list) {
			/*
			 * We have to drop the spinlock, so entries can be
			 * removed.  They can't be freed, though, so we just
			 * keep the forward pointers and zero the back ones -
			 * that way we can get to the rest of the list.
			 */
			if (!orangefs_sb->list.prev)
				continue;
			gossip_debug(GOSSIP_DEV_DEBUG,
				     "%s: Remounting SB %p\n",
				     __func__,
				     orangefs_sb);

			spin_unlock(&orangefs_superblocks_lock);
			ret = orangefs_remount(orangefs_sb);
			spin_lock(&orangefs_superblocks_lock);
			if (ret) {
				gossip_debug(GOSSIP_DEV_DEBUG,
					     "SB %p remount failed\n",
					     orangefs_sb);
				break;
			}
		}
		spin_unlock(&orangefs_superblocks_lock);
		gossip_debug(GOSSIP_DEV_DEBUG,
			     "%s: priority remount complete\n",
			     __func__);
		mutex_unlock(&request_mutex);
		return ret;

	case ORANGEFS_DEV_UPSTREAM:
		ret = copy_to_user((void __user *)arg,
				    &upstream_kmod,
				    sizeof(upstream_kmod));

		if (ret != 0)
			return -EIO;
		else
			return ret;

	case ORANGEFS_DEV_CLIENT_MASK:
		ret = copy_from_user(&mask2_info,
				     (void __user *)arg,
				     sizeof(struct dev_mask2_info_s));

		if (ret != 0)
			return -EIO;

		client_debug_mask.mask1 = mask2_info.mask1_value;
		client_debug_mask.mask2 = mask2_info.mask2_value;

		pr_info("%s: client debug mask has been been received "
			":%llx: :%llx:\n",
			__func__,
			(unsigned long long)client_debug_mask.mask1,
			(unsigned long long)client_debug_mask.mask2);

		return ret;

	case ORANGEFS_DEV_CLIENT_STRING:
		ret = copy_from_user(&client_debug_array_string,
				     (void __user *)arg,
				     ORANGEFS_MAX_DEBUG_STRING_LEN);
		/*
		 * The real client-core makes an effort to ensure
		 * that actual strings that aren't too long to fit in
		 * this buffer is what we get here. We're going to use
		 * string functions on the stuff we got, so we'll make
		 * this extra effort to try and keep from
		 * flowing out of this buffer when we use the string
		 * functions, even if somehow the stuff we end up
		 * with here is garbage.
		 */
		client_debug_array_string[ORANGEFS_MAX_DEBUG_STRING_LEN - 1] =
			'\0';
		
		if (ret != 0) {
			pr_info("%s: CLIENT_STRING: copy_from_user failed\n",
				__func__);
			return -EIO;
		}

		pr_info("%s: client debug array string has been received.\n",
			__func__);

		if (!help_string_initialized) {

			/* Free the "we don't know yet" default string... */
			kfree(debug_help_string);

			/* build a proper debug help string */
			if (orangefs_prepare_debugfs_help_string(0)) {
				gossip_err("%s: no debug help string \n",
					   __func__);
				return -EIO;
			}

			/* Replace the boilerplate boot-time debug-help file. */
			debugfs_remove(help_file_dentry);

			help_file_dentry =
				debugfs_create_file(
					ORANGEFS_KMOD_DEBUG_HELP_FILE,
					0444,
					debug_dir,
					debug_help_string,
					&debug_help_fops);

			if (!help_file_dentry) {
				gossip_err("%s: debugfs_create_file failed for"
					   " :%s:!\n",
					   __func__,
					   ORANGEFS_KMOD_DEBUG_HELP_FILE);
				return -EIO;
			}
		}

		debug_mask_to_string(&client_debug_mask, 1);

		debugfs_remove(client_debug_dentry);

		orangefs_client_debug_init();

		help_string_initialized++;

		return ret;

	case ORANGEFS_DEV_DEBUG:
		ret = copy_from_user(&mask_info,
				     (void __user *)arg,
				     sizeof(mask_info));

		if (ret != 0)
			return -EIO;

		if (mask_info.mask_type == KERNEL_MASK) {
			if ((mask_info.mask_value == 0)
			    && (kernel_mask_set_mod_init)) {
				/*
				 * the kernel debug mask was set when the
				 * kernel module was loaded; don't override
				 * it if the client-core was started without
				 * a value for ORANGEFS_KMODMASK.
				 */
				return 0;
			}
			debug_mask_to_string(&mask_info.mask_value,
					     mask_info.mask_type);
			gossip_debug_mask = mask_info.mask_value;
			pr_info("%s: kernel debug mask has been modified to "
				":%s: :%llx:\n",
				__func__,
				kernel_debug_string,
				(unsigned long long)gossip_debug_mask);
		} else if (mask_info.mask_type == CLIENT_MASK) {
			debug_mask_to_string(&mask_info.mask_value,
					     mask_info.mask_type);
			pr_info("%s: client debug mask has been modified to"
				":%s: :%llx:\n",
				__func__,
				client_debug_string,
				llu(mask_info.mask_value));
		} else {
			gossip_lerr("Invalid mask type....\n");
			return -EINVAL;
		}

		return ret;

	default:
		return -ENOIOCTLCMD;
	}
	return -ENOIOCTLCMD;
}

static long orangefs_devreq_ioctl(struct file *file,
			       unsigned int command, unsigned long arg)
{
	long ret;

	/* Check for properly constructed commands */
	ret = check_ioctl_command(command);
	if (ret < 0)
		return (int)ret;

	return (int)dispatch_ioctl_command(command, arg);
}

#ifdef CONFIG_COMPAT		/* CONFIG_COMPAT is in .config */

/*  Compat structure for the ORANGEFS_DEV_MAP ioctl */
struct ORANGEFS_dev_map_desc32 {
	compat_uptr_t ptr;
	__s32 total_size;
	__s32 size;
	__s32 count;
};

static unsigned long translate_dev_map26(unsigned long args, long *error)
{
	struct ORANGEFS_dev_map_desc32 __user *p32 = (void __user *)args;
	/*
	 * Depending on the architecture, allocate some space on the
	 * user-call-stack based on our expected layout.
	 */
	struct ORANGEFS_dev_map_desc __user *p =
	    compat_alloc_user_space(sizeof(*p));
	compat_uptr_t addr;

	*error = 0;
	/* get the ptr from the 32 bit user-space */
	if (get_user(addr, &p32->ptr))
		goto err;
	/* try to put that into a 64-bit layout */
	if (put_user(compat_ptr(addr), &p->ptr))
		goto err;
	/* copy the remaining fields */
	if (copy_in_user(&p->total_size, &p32->total_size, sizeof(__s32)))
		goto err;
	if (copy_in_user(&p->size, &p32->size, sizeof(__s32)))
		goto err;
	if (copy_in_user(&p->count, &p32->count, sizeof(__s32)))
		goto err;
	return (unsigned long)p;
err:
	*error = -EFAULT;
	return 0;
}

/*
 * 32 bit user-space apps' ioctl handlers when kernel modules
 * is compiled as a 64 bit one
 */
static long orangefs_devreq_compat_ioctl(struct file *filp, unsigned int cmd,
				      unsigned long args)
{
	long ret;
	unsigned long arg = args;

	/* Check for properly constructed commands */
	ret = check_ioctl_command(cmd);
	if (ret < 0)
		return ret;
	if (cmd == ORANGEFS_DEV_MAP) {
		/*
		 * convert the arguments to what we expect internally
		 * in kernel space
		 */
		arg = translate_dev_map26(args, &ret);
		if (ret < 0) {
			gossip_err("Could not translate dev map\n");
			return ret;
		}
	}
	/* no other ioctl requires translation */
	return dispatch_ioctl_command(cmd, arg);
}

#endif /* CONFIG_COMPAT is in .config */

/* the assigned character device major number */
static int orangefs_dev_major;

/*
 * Initialize orangefs device specific state:
 * Must be called at module load time only
 */
int orangefs_dev_init(void)
{
	/* register orangefs-req device  */
	orangefs_dev_major = register_chrdev(0,
					  ORANGEFS_REQDEVICE_NAME,
					  &orangefs_devreq_file_operations);
	if (orangefs_dev_major < 0) {
		gossip_debug(GOSSIP_DEV_DEBUG,
			     "Failed to register /dev/%s (error %d)\n",
			     ORANGEFS_REQDEVICE_NAME, orangefs_dev_major);
		return orangefs_dev_major;
	}

	gossip_debug(GOSSIP_DEV_DEBUG,
		     "*** /dev/%s character device registered ***\n",
		     ORANGEFS_REQDEVICE_NAME);
	gossip_debug(GOSSIP_DEV_DEBUG, "'mknod /dev/%s c %d 0'.\n",
		     ORANGEFS_REQDEVICE_NAME, orangefs_dev_major);
	return 0;
}

void orangefs_dev_cleanup(void)
{
	unregister_chrdev(orangefs_dev_major, ORANGEFS_REQDEVICE_NAME);
	gossip_debug(GOSSIP_DEV_DEBUG,
		     "*** /dev/%s character device unregistered ***\n",
		     ORANGEFS_REQDEVICE_NAME);
}

static unsigned int orangefs_devreq_poll(struct file *file,
				      struct poll_table_struct *poll_table)
{
	int poll_revent_mask = 0;

	poll_wait(file, &orangefs_request_list_waitq, poll_table);

	if (!list_empty(&orangefs_request_list))
		poll_revent_mask |= POLL_IN;
	return poll_revent_mask;
}

const struct file_operations orangefs_devreq_file_operations = {
	.owner = THIS_MODULE,
	.read = orangefs_devreq_read,
	.write_iter = orangefs_devreq_write_iter,
	.open = orangefs_devreq_open,
	.release = orangefs_devreq_release,
	.unlocked_ioctl = orangefs_devreq_ioctl,

#ifdef CONFIG_COMPAT		/* CONFIG_COMPAT is in .config */
	.compat_ioctl = orangefs_devreq_compat_ioctl,
#endif
	.poll = orangefs_devreq_poll
};
