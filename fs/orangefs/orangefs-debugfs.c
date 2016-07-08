/*
 * What:		/sys/kernel/debug/orangefs/debug-help
 * Date:		June 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			List of client and kernel debug keywords.
 *
 *
 * What:		/sys/kernel/debug/orangefs/client-debug
 * Date:		June 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			Debug setting for "the client", the userspace
 * 			helper for the kernel module.
 *
 *
 * What:		/sys/kernel/debug/orangefs/kernel-debug
 * Date:		June 2015
 * Contact:		Mike Marshall <hubcap@omnibond.com>
 * Description:
 * 			Debug setting for the orangefs kernel module.
 *
 * 			Any of the keywords, or comma-separated lists
 * 			of keywords, from debug-help can be catted to
 * 			client-debug or kernel-debug.
 *
 * 			"none", "all" and "verbose" are special keywords
 * 			for client-debug. Setting client-debug to "all"
 * 			is kind of like trying to drink water from a
 * 			fire hose, "verbose" triggers most of the same
 * 			output except for the constant flow of output
 * 			from the main wait loop.
 *
 * 			"none" and "all" are similar settings for kernel-debug
 * 			no need for a "verbose".
 */
#include <linux/debugfs.h>
#include <linux/slab.h>

#include <linux/uaccess.h>

#include "orangefs-debugfs.h"
#include "protocol.h"
#include "orangefs-kernel.h"

static int orangefs_debug_disabled = 1;

static int orangefs_debug_help_open(struct inode *, struct file *);

const struct file_operations debug_help_fops = {
	.open           = orangefs_debug_help_open,
	.read           = seq_read,
	.release        = seq_release,
	.llseek         = seq_lseek,
};

static void *help_start(struct seq_file *, loff_t *);
static void *help_next(struct seq_file *, void *, loff_t *);
static void help_stop(struct seq_file *, void *);
static int help_show(struct seq_file *, void *);

static const struct seq_operations help_debug_ops = {
	.start	= help_start,
	.next	= help_next,
	.stop	= help_stop,
	.show	= help_show,
};

/*
 * Used to protect data in ORANGEFS_KMOD_DEBUG_FILE and
 * ORANGEFS_KMOD_DEBUG_FILE.
 */
static DEFINE_MUTEX(orangefs_debug_lock);

int orangefs_debug_open(struct inode *, struct file *);

static ssize_t orangefs_debug_read(struct file *,
				 char __user *,
				 size_t,
				 loff_t *);

static ssize_t orangefs_debug_write(struct file *,
				  const char __user *,
				  size_t,
				  loff_t *);

static const struct file_operations kernel_debug_fops = {
	.open           = orangefs_debug_open,
	.read           = orangefs_debug_read,
	.write		= orangefs_debug_write,
	.llseek         = generic_file_llseek,
};

/*
 * initialize kmod debug operations, create orangefs debugfs dir and
 * ORANGEFS_KMOD_DEBUG_HELP_FILE.
 */
int orangefs_debugfs_init(void)
{

	int rc = -ENOMEM;

	debug_dir = debugfs_create_dir("orangefs", NULL);
	if (!debug_dir) {
		pr_info("%s: debugfs_create_dir failed.\n", __func__);
		goto out;
	}

	help_file_dentry = debugfs_create_file(ORANGEFS_KMOD_DEBUG_HELP_FILE,
				  0444,
				  debug_dir,
				  debug_help_string,
				  &debug_help_fops);
	if (!help_file_dentry) {
		pr_info("%s: debugfs_create_file failed.\n", __func__);
		goto out;
	}

	orangefs_debug_disabled = 0;
	rc = 0;

out:

	return rc;
}

void orangefs_debugfs_cleanup(void)
{
	debugfs_remove_recursive(debug_dir);
}

/* open ORANGEFS_KMOD_DEBUG_HELP_FILE */
static int orangefs_debug_help_open(struct inode *inode, struct file *file)
{
	int rc = -ENODEV;
	int ret;

	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		     "orangefs_debug_help_open: start\n");

	if (orangefs_debug_disabled)
		goto out;

	ret = seq_open(file, &help_debug_ops);
	if (ret)
		goto out;

	((struct seq_file *)(file->private_data))->private = inode->i_private;

	rc = 0;

out:
	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		     "orangefs_debug_help_open: rc:%d:\n",
		     rc);
	return rc;
}

/*
 * I think start always gets called again after stop. Start
 * needs to return NULL when it is done. The whole "payload"
 * in this case is a single (long) string, so by the second
 * time we get to start (pos = 1), we're done.
 */
static void *help_start(struct seq_file *m, loff_t *pos)
{
	void *payload = NULL;

	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "help_start: start\n");

	if (*pos == 0)
		payload = m->private;

	return payload;
}

static void *help_next(struct seq_file *m, void *v, loff_t *pos)
{
	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "help_next: start\n");

	return NULL;
}

static void help_stop(struct seq_file *m, void *p)
{
	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "help_stop: start\n");
}

static int help_show(struct seq_file *m, void *v)
{
	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "help_show: start\n");

	seq_puts(m, v);

	return 0;
}

/*
 * initialize the kernel-debug file.
 */
int orangefs_kernel_debug_init(void)
{
	int rc = -ENOMEM;
	struct dentry *ret;
	char *k_buffer = NULL;

	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "%s: start\n", __func__);

	k_buffer = kzalloc(ORANGEFS_MAX_DEBUG_STRING_LEN, GFP_KERNEL);
	if (!k_buffer)
		goto out;

	if (strlen(kernel_debug_string) + 1 < ORANGEFS_MAX_DEBUG_STRING_LEN) {
		strcpy(k_buffer, kernel_debug_string);
		strcat(k_buffer, "\n");
	} else {
		strcpy(k_buffer, "none\n");
		pr_info("%s: overflow 1!\n", __func__);
	}

	ret = debugfs_create_file(ORANGEFS_KMOD_DEBUG_FILE,
				  0444,
				  debug_dir,
				  k_buffer,
				  &kernel_debug_fops);
	if (!ret) {
		pr_info("%s: failed to create %s.\n",
			__func__,
			ORANGEFS_KMOD_DEBUG_FILE);
		goto out;
	}

	rc = 0;

out:

	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "%s: rc:%d:\n", __func__, rc);
	return rc;
}

/*
 * initialize the client-debug file.
 */
int orangefs_client_debug_init(void)
{

	int rc = -ENOMEM;
	char *c_buffer = NULL;

	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "%s: start\n", __func__);

	c_buffer = kzalloc(ORANGEFS_MAX_DEBUG_STRING_LEN, GFP_KERNEL);
	if (!c_buffer)
		goto out;

	if (strlen(client_debug_string) + 1 < ORANGEFS_MAX_DEBUG_STRING_LEN) {
		strcpy(c_buffer, client_debug_string);
		strcat(c_buffer, "\n");
	} else {
		strcpy(c_buffer, "none\n");
		pr_info("%s: overflow! 2\n", __func__);
	}

	client_debug_dentry = debugfs_create_file(ORANGEFS_CLIENT_DEBUG_FILE,
						  0444,
						  debug_dir,
						  c_buffer,
						  &kernel_debug_fops);
	if (!client_debug_dentry) {
		pr_info("%s: failed to create updated %s.\n",
			__func__,
			ORANGEFS_CLIENT_DEBUG_FILE);
		goto out;
	}

	rc = 0;

out:

	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "%s: rc:%d:\n", __func__, rc);
	return rc;
}

/* open ORANGEFS_KMOD_DEBUG_FILE or ORANGEFS_CLIENT_DEBUG_FILE.*/
int orangefs_debug_open(struct inode *inode, struct file *file)
{
	int rc = -ENODEV;

	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		     "%s: orangefs_debug_disabled: %d\n",
		     __func__,
		     orangefs_debug_disabled);

	if (orangefs_debug_disabled)
		goto out;

	rc = 0;
	mutex_lock(&orangefs_debug_lock);
	file->private_data = inode->i_private;
	mutex_unlock(&orangefs_debug_lock);

out:
	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		     "orangefs_debug_open: rc: %d\n",
		     rc);
	return rc;
}

static ssize_t orangefs_debug_read(struct file *file,
				 char __user *ubuf,
				 size_t count,
				 loff_t *ppos)
{
	char *buf;
	int sprintf_ret;
	ssize_t read_ret = -ENOMEM;

	gossip_debug(GOSSIP_DEBUGFS_DEBUG, "orangefs_debug_read: start\n");

	buf = kmalloc(ORANGEFS_MAX_DEBUG_STRING_LEN, GFP_KERNEL);
	if (!buf)
		goto out;

	mutex_lock(&orangefs_debug_lock);
	sprintf_ret = sprintf(buf, "%s", (char *)file->private_data);
	mutex_unlock(&orangefs_debug_lock);

	read_ret = simple_read_from_buffer(ubuf, count, ppos, buf, sprintf_ret);

	kfree(buf);

out:
	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		     "orangefs_debug_read: ret: %zu\n",
		     read_ret);

	return read_ret;
}

static ssize_t orangefs_debug_write(struct file *file,
				  const char __user *ubuf,
				  size_t count,
				  loff_t *ppos)
{
	char *buf;
	int rc = -EFAULT;
	size_t silly = 0;
	char *debug_string;
	struct orangefs_kernel_op_s *new_op = NULL;
	struct client_debug_mask c_mask = { NULL, 0, 0 };

	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		"orangefs_debug_write: %s\n",
		file->f_path.dentry->d_name.name);

	/*
	 * Thwart users who try to jamb a ridiculous number
	 * of bytes into the debug file...
	 */
	if (count > ORANGEFS_MAX_DEBUG_STRING_LEN + 1) {
		silly = count;
		count = ORANGEFS_MAX_DEBUG_STRING_LEN + 1;
	}

	buf = kzalloc(ORANGEFS_MAX_DEBUG_STRING_LEN, GFP_KERNEL);
	if (!buf)
		goto out;

	if (copy_from_user(buf, ubuf, count - 1)) {
		gossip_debug(GOSSIP_DEBUGFS_DEBUG,
			     "%s: copy_from_user failed!\n",
			     __func__);
		goto out;
	}

	/*
	 * Map the keyword string from userspace into a valid debug mask.
	 * The mapping process involves mapping the human-inputted string
	 * into a valid mask, and then rebuilding the string from the
	 * verified valid mask.
	 *
	 * A service operation is required to set a new client-side
	 * debug mask.
	 */
	if (!strcmp(file->f_path.dentry->d_name.name,
		    ORANGEFS_KMOD_DEBUG_FILE)) {
		debug_string_to_mask(buf, &gossip_debug_mask, 0);
		debug_mask_to_string(&gossip_debug_mask, 0);
		debug_string = kernel_debug_string;
		gossip_debug(GOSSIP_DEBUGFS_DEBUG,
			     "New kernel debug string is %s\n",
			     kernel_debug_string);
	} else {
		/* Can't reset client debug mask if client is not running. */
		if (is_daemon_in_service()) {
			pr_info("%s: Client not running :%d:\n",
				__func__,
				is_daemon_in_service());
			goto out;
		}

		debug_string_to_mask(buf, &c_mask, 1);
		debug_mask_to_string(&c_mask, 1);
		debug_string = client_debug_string;

		new_op = op_alloc(ORANGEFS_VFS_OP_PARAM);
		if (!new_op) {
			pr_info("%s: op_alloc failed!\n", __func__);
			goto out;
		}

		new_op->upcall.req.param.op =
			ORANGEFS_PARAM_REQUEST_OP_TWO_MASK_VALUES;
		new_op->upcall.req.param.type = ORANGEFS_PARAM_REQUEST_SET;
		memset(new_op->upcall.req.param.s_value,
		       0,
		       ORANGEFS_MAX_DEBUG_STRING_LEN);
		sprintf(new_op->upcall.req.param.s_value,
			"%llx %llx\n",
			c_mask.mask1,
			c_mask.mask2);

		/* service_operation returns 0 on success... */
		rc = service_operation(new_op,
				       "orangefs_param",
					ORANGEFS_OP_INTERRUPTIBLE);

		if (rc)
			gossip_debug(GOSSIP_DEBUGFS_DEBUG,
				     "%s: service_operation failed! rc:%d:\n",
				     __func__,
				     rc);

		op_release(new_op);
	}

	mutex_lock(&orangefs_debug_lock);
	memset(file->f_inode->i_private, 0, ORANGEFS_MAX_DEBUG_STRING_LEN);
	sprintf((char *)file->f_inode->i_private, "%s\n", debug_string);
	mutex_unlock(&orangefs_debug_lock);

	*ppos += count;
	if (silly)
		rc = silly;
	else
		rc = count;

out:
	gossip_debug(GOSSIP_DEBUGFS_DEBUG,
		     "orangefs_debug_write: rc: %d\n",
		     rc);
	kfree(buf);
	return rc;
}
