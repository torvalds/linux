/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) Test Control Interface.
 *
 *  The 'splat' (Solaris Porting LAyer Tests) module is designed as a
 *  framework which runs various in kernel regression tests to validate
 *  the SPL primitives honor the Solaris ABI.
 *
 *  The splat module is constructed of various splat_* source files each
 *  of which contain regression tests for a particular subsystem.  For
 *  example, the splat_kmem.c file contains all the tests for validating
 *  the kmem interfaces have been implemented correctly.  When the splat
 *  module is loaded splat_*_init() will be called for each subsystems
 *  tests.  It is the responsibility of splat_*_init() to register all
 *  the tests for this subsystem using the splat_test_init().
 *  Similarly splat_*_fini() is called when the splat module is removed
 *  and is responsible for unregistering its tests via the splat_test_fini.
 *  Once a test is registered it can then be run with an ioctl()
 *  call which specifies the subsystem and test to be run.  The provided
 *  splat command line tool can be used to display all available
 *  subsystems and tests.  It can also be used to run the full suite
 *  of regression tests or particular tests.
\*****************************************************************************/

#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include "splat-internal.h"

static struct list_head splat_module_list;
static spinlock_t splat_module_lock;

static int
splat_open(struct inode *inode, struct file *file)
{
	splat_info_t *info;

	info = (splat_info_t *)kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	mutex_init(&info->info_lock, SPLAT_NAME, MUTEX_DEFAULT, NULL);
	info->info_size = SPLAT_INFO_BUFFER_SIZE;
	info->info_buffer = (char *)vmalloc(SPLAT_INFO_BUFFER_SIZE);
	if (info->info_buffer == NULL) {
		kfree(info);
		return -ENOMEM;
	}
	memset(info->info_buffer, 0, info->info_size);

	info->info_head = info->info_buffer;
	file->private_data = (void *)info;

	splat_print(file, "%s\n", spl_version);

	return 0;
}

static int
splat_release(struct inode *inode, struct file *file)
{
	splat_info_t *info = (splat_info_t *)file->private_data;

	ASSERT(info);
	ASSERT(info->info_buffer);

	mutex_destroy(&info->info_lock);
	vfree(info->info_buffer);
	kfree(info);

	return 0;
}

static int
splat_buffer_clear(struct file *file, splat_cfg_t *kcfg, unsigned long arg)
{
	splat_info_t *info = (splat_info_t *)file->private_data;

	ASSERT(info);
	ASSERT(info->info_buffer);

	mutex_enter(&info->info_lock);
	memset(info->info_buffer, 0, info->info_size);
	info->info_head = info->info_buffer;
	mutex_exit(&info->info_lock);

	return 0;
}

static int
splat_buffer_size(struct file *file, splat_cfg_t *kcfg, unsigned long arg)
{
	splat_info_t *info = (splat_info_t *)file->private_data;
	char *buf;
	int min, size, rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	mutex_enter(&info->info_lock);
	if (kcfg->cfg_arg1 > 0) {

		size = kcfg->cfg_arg1;
		buf = (char *)vmalloc(size);
		if (buf == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		/* Zero fill and truncate contents when coping buffer */
		min = ((size < info->info_size) ? size : info->info_size);
		memset(buf, 0, size);
		memcpy(buf, info->info_buffer, min);
		vfree(info->info_buffer);
		info->info_size = size;
		info->info_buffer = buf;
		info->info_head = info->info_buffer;
	}

	kcfg->cfg_rc1 = info->info_size;

	if (copy_to_user((struct splat_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		rc = -EFAULT;
out:
	mutex_exit(&info->info_lock);

	return rc;
}


static splat_subsystem_t *
splat_subsystem_find(int id) {
	splat_subsystem_t *sub;

        spin_lock(&splat_module_lock);
        list_for_each_entry(sub, &splat_module_list, subsystem_list) {
		if (id == sub->desc.id) {
		        spin_unlock(&splat_module_lock);
			return sub;
		}
        }
        spin_unlock(&splat_module_lock);

	return NULL;
}

static int
splat_subsystem_count(splat_cfg_t *kcfg, unsigned long arg)
{
	splat_subsystem_t *sub;
	int i = 0;

        spin_lock(&splat_module_lock);
        list_for_each_entry(sub, &splat_module_list, subsystem_list)
		i++;

        spin_unlock(&splat_module_lock);
	kcfg->cfg_rc1 = i;

	if (copy_to_user((struct splat_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		return -EFAULT;

	return 0;
}

static int
splat_subsystem_list(splat_cfg_t *kcfg, unsigned long arg)
{
	splat_subsystem_t *sub;
	splat_cfg_t *tmp;
	int size, i = 0;

	/* Structure will be sized large enough for N subsystem entries
	 * which is passed in by the caller.  On exit the number of
	 * entries filled in with valid subsystems will be stored in
	 * cfg_rc1.  If the caller does not provide enough entries
	 * for all subsystems we will truncate the list to avoid overrun.
	 */
	size = sizeof(*tmp) + kcfg->cfg_data.splat_subsystems.size *
	       sizeof(splat_user_t);
	tmp = kmalloc(size, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	/* Local 'tmp' is used as the structure copied back to user space */
	memset(tmp, 0, size);
	memcpy(tmp, kcfg, sizeof(*kcfg));

        spin_lock(&splat_module_lock);
        list_for_each_entry(sub, &splat_module_list, subsystem_list) {
		strncpy(tmp->cfg_data.splat_subsystems.descs[i].name,
		        sub->desc.name, SPLAT_NAME_SIZE);
		strncpy(tmp->cfg_data.splat_subsystems.descs[i].desc,
		        sub->desc.desc, SPLAT_DESC_SIZE);
		tmp->cfg_data.splat_subsystems.descs[i].id = sub->desc.id;

		/* Truncate list if we are about to overrun alloc'ed memory */
		if ((i++) == kcfg->cfg_data.splat_subsystems.size)
			break;
        }
        spin_unlock(&splat_module_lock);
	tmp->cfg_rc1 = i;

	if (copy_to_user((struct splat_cfg_t __user *)arg, tmp, size)) {
		kfree(tmp);
		return -EFAULT;
	}

	kfree(tmp);
	return 0;
}

static int
splat_test_count(splat_cfg_t *kcfg, unsigned long arg)
{
	splat_subsystem_t *sub;
	splat_test_t *test;
	int i = 0;

	/* Subsystem ID passed as arg1 */
	sub = splat_subsystem_find(kcfg->cfg_arg1);
	if (sub == NULL)
		return -EINVAL;

        spin_lock(&(sub->test_lock));
        list_for_each_entry(test, &(sub->test_list), test_list)
		i++;

        spin_unlock(&(sub->test_lock));
	kcfg->cfg_rc1 = i;

	if (copy_to_user((struct splat_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		return -EFAULT;

	return 0;
}

static int
splat_test_list(splat_cfg_t *kcfg, unsigned long arg)
{
	splat_subsystem_t *sub;
	splat_test_t *test;
	splat_cfg_t *tmp;
	int size, i = 0;

	/* Subsystem ID passed as arg1 */
	sub = splat_subsystem_find(kcfg->cfg_arg1);
	if (sub == NULL)
		return -EINVAL;

	/* Structure will be sized large enough for N test entries
	 * which is passed in by the caller.  On exit the number of
	 * entries filled in with valid tests will be stored in
	 * cfg_rc1.  If the caller does not provide enough entries
	 * for all tests we will truncate the list to avoid overrun.
	 */
	size = sizeof(*tmp)+kcfg->cfg_data.splat_tests.size*sizeof(splat_user_t);
	tmp = kmalloc(size, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	/* Local 'tmp' is used as the structure copied back to user space */
	memset(tmp, 0, size);
	memcpy(tmp, kcfg, sizeof(*kcfg));

        spin_lock(&(sub->test_lock));
        list_for_each_entry(test, &(sub->test_list), test_list) {
		strncpy(tmp->cfg_data.splat_tests.descs[i].name,
		        test->desc.name, SPLAT_NAME_SIZE);
		strncpy(tmp->cfg_data.splat_tests.descs[i].desc,
		        test->desc.desc, SPLAT_DESC_SIZE);
		tmp->cfg_data.splat_tests.descs[i].id = test->desc.id;

		/* Truncate list if we are about to overrun alloc'ed memory */
		if ((i++) == kcfg->cfg_data.splat_tests.size)
			break;
        }
        spin_unlock(&(sub->test_lock));
	tmp->cfg_rc1 = i;

	if (copy_to_user((struct splat_cfg_t __user *)arg, tmp, size)) {
		kfree(tmp);
		return -EFAULT;
	}

	kfree(tmp);
	return 0;
}

static int
splat_validate(struct file *file, splat_subsystem_t *sub, int cmd, void *arg)
{
        splat_test_t *test;

        spin_lock(&(sub->test_lock));
        list_for_each_entry(test, &(sub->test_list), test_list) {
                if (test->desc.id == cmd) {
			spin_unlock(&(sub->test_lock));
                        return test->test(file, arg);
                }
        }
        spin_unlock(&(sub->test_lock));

        return -EINVAL;
}

static int
splat_ioctl_cfg(struct file *file, unsigned int cmd, unsigned long arg)
{
	splat_cfg_t kcfg;
	int rc = 0;

	/* User and kernel space agree about arg size */
	if (_IOC_SIZE(cmd) != sizeof(kcfg))
		return -EBADMSG;

	if (copy_from_user(&kcfg, (splat_cfg_t *)arg, sizeof(kcfg)))
		return -EFAULT;

	if (kcfg.cfg_magic != SPLAT_CFG_MAGIC) {
		splat_print(file, "Bad config magic 0x%x != 0x%x\n",
		          kcfg.cfg_magic, SPLAT_CFG_MAGIC);
		return -EINVAL;
	}

	switch (kcfg.cfg_cmd) {
		case SPLAT_CFG_BUFFER_CLEAR:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Unused
			 */
			rc = splat_buffer_clear(file, &kcfg, arg);
			break;
		case SPLAT_CFG_BUFFER_SIZE:
			/* cfg_arg1 - 0 - query size; >0 resize
			 * cfg_rc1  - Set to current buffer size
			 */
			rc = splat_buffer_size(file, &kcfg, arg);
			break;
		case SPLAT_CFG_SUBSYSTEM_COUNT:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Set to number of subsystems
			 */
			rc = splat_subsystem_count(&kcfg, arg);
			break;
		case SPLAT_CFG_SUBSYSTEM_LIST:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Set to number of subsystems
			 * cfg_data.splat_subsystems - Set with subsystems
			 */
			rc = splat_subsystem_list(&kcfg, arg);
			break;
		case SPLAT_CFG_TEST_COUNT:
			/* cfg_arg1 - Set to a target subsystem
			 * cfg_rc1  - Set to number of tests
			 */
			rc = splat_test_count(&kcfg, arg);
			break;
		case SPLAT_CFG_TEST_LIST:
			/* cfg_arg1 - Set to a target subsystem
			 * cfg_rc1  - Set to number of tests
			 * cfg_data.splat_subsystems - Populated with tests
			 */
			rc = splat_test_list(&kcfg, arg);
			break;
		default:
			splat_print(file, "Bad config command %d\n",
				    kcfg.cfg_cmd);
			rc = -EINVAL;
			break;
	}

	return rc;
}

static int
splat_ioctl_cmd(struct file *file, unsigned int cmd, unsigned long arg)
{
	splat_subsystem_t *sub;
	splat_cmd_t kcmd;
	int rc = -EINVAL;
	void *data = NULL;

	/* User and kernel space agree about arg size */
	if (_IOC_SIZE(cmd) != sizeof(kcmd))
		return -EBADMSG;

	if (copy_from_user(&kcmd, (splat_cfg_t *)arg, sizeof(kcmd)))
		return -EFAULT;

	if (kcmd.cmd_magic != SPLAT_CMD_MAGIC) {
		splat_print(file, "Bad command magic 0x%x != 0x%x\n",
		          kcmd.cmd_magic, SPLAT_CFG_MAGIC);
		return -EINVAL;
	}

	/* Allocate memory for any opaque data the caller needed to pass on */
	if (kcmd.cmd_data_size > 0) {
		data = (void *)kmalloc(kcmd.cmd_data_size, GFP_KERNEL);
		if (data == NULL)
			return -ENOMEM;

		if (copy_from_user(data, (void *)(arg + offsetof(splat_cmd_t,
		                   cmd_data_str)), kcmd.cmd_data_size)) {
			kfree(data);
			return -EFAULT;
		}
	}

	sub = splat_subsystem_find(kcmd.cmd_subsystem);
	if (sub != NULL)
		rc = splat_validate(file, sub, kcmd.cmd_test, data);
	else
		rc = -EINVAL;

	if (data != NULL)
		kfree(data);

	return rc;
}

static long
splat_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = 0;

	/* Ignore tty ioctls */
	if ((cmd & 0xffffff00) == ((int)'T') << 8)
		return -ENOTTY;

	switch (cmd) {
		case SPLAT_CFG:
			rc = splat_ioctl_cfg(file, cmd, arg);
			break;
		case SPLAT_CMD:
			rc = splat_ioctl_cmd(file, cmd, arg);
			break;
		default:
			splat_print(file, "Bad ioctl command %d\n", cmd);
			rc = -EINVAL;
			break;
	}

	return rc;
}

#ifdef CONFIG_COMPAT
/* Compatibility handler for ioctls from 32-bit ELF binaries */
static long
splat_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return splat_unlocked_ioctl(file, cmd, arg);
}
#endif /* CONFIG_COMPAT */

/* I'm not sure why you would want to write in to this buffer from
 * user space since its principle use is to pass test status info
 * back to the user space, but I don't see any reason to prevent it.
 */
static ssize_t splat_write(struct file *file, const char __user *buf,
                         size_t count, loff_t *ppos)
{
	splat_info_t *info = (splat_info_t *)file->private_data;
	int rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	mutex_enter(&info->info_lock);

	/* Write beyond EOF */
	if (*ppos >= info->info_size) {
		rc = -EFBIG;
		goto out;
	}

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_from_user(info->info_buffer, buf, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	mutex_exit(&info->info_lock);
	return rc;
}

static ssize_t splat_read(struct file *file, char __user *buf,
		        size_t count, loff_t *ppos)
{
	splat_info_t *info = (splat_info_t *)file->private_data;
	int rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	mutex_enter(&info->info_lock);

	/* Read beyond EOF */
	if (*ppos >= info->info_size)
		goto out;

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_to_user(buf, info->info_buffer + *ppos, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	mutex_exit(&info->info_lock);
	return rc;
}

static loff_t splat_seek(struct file *file, loff_t offset, int origin)
{
	splat_info_t *info = (splat_info_t *)file->private_data;
	int rc = -EINVAL;

	ASSERT(info);
	ASSERT(info->info_buffer);

	mutex_enter(&info->info_lock);

	switch (origin) {
	case 0: /* SEEK_SET - No-op just do it */
		break;
	case 1: /* SEEK_CUR - Seek from current */
		offset = file->f_pos + offset;
		break;
	case 2: /* SEEK_END - Seek from end */
		offset = info->info_size + offset;
		break;
	}

	if (offset >= 0) {
		file->f_pos = offset;
		file->f_version = 0;
		rc = offset;
	}

	mutex_exit(&info->info_lock);

	return rc;
}

static struct file_operations splat_fops = {
	.owner		= THIS_MODULE,
	.open		= splat_open,
	.release	= splat_release,
	.unlocked_ioctl	= splat_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= splat_compat_ioctl,
#endif
	.read		= splat_read,
	.write		= splat_write,
	.llseek		= splat_seek,
};

static struct miscdevice splat_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= SPLAT_NAME,
	.fops		= &splat_fops,
};

static void splat_subsystem_init(const char *name,
    splat_subsystem_t *(*init)(void))
{
	splat_subsystem_t *sub;
	sub = init();
	if (sub == NULL) {
		printk(KERN_ERR "splat: Error initializing: %s\n", name);
		return;
	}
	spin_lock(&splat_module_lock);
	list_add_tail(&sub->subsystem_list, &splat_module_list);
	spin_unlock(&splat_module_lock);
}

static void splat_subsystem_fini(const char *name,
    int (*id_func)(void), void (*fini)(splat_subsystem_t *))
{
	splat_subsystem_t *sub, *tmp;
	int id, flag = 0;

	id = id_func();
	spin_lock(&splat_module_lock);
	list_for_each_entry_safe(sub, tmp, &splat_module_list, subsystem_list) {
		if (sub->desc.id == id) {
			list_del_init(&sub->subsystem_list);
			flag = 1;
			break;
		}
	}
	spin_unlock(&splat_module_lock);
	if (flag == 0)
		printk(KERN_ERR "splat: Error finalizing: %s\n", name);
	else
		fini(sub);
}

#define SPLAT_SUBSYSTEM_INIT(type) \
	splat_subsystem_init(#type, splat_##type##_init)
#define SPLAT_SUBSYSTEM_FINI(type) \
	splat_subsystem_fini(#type, splat_##type##_id, splat_##type##_fini)

void splat_test_init(splat_subsystem_t *sub, const char *name,
    const char *desc, unsigned int tid, splat_test_func_t func)
{
	splat_test_t *test;
	test = kmalloc(sizeof (splat_test_t), GFP_KERNEL);
	if (test == NULL) {
		printk(KERN_ERR "splat: Error initializing: %s/%u\n",
		    name, tid);
		return;
	}
	memset(test, 0, sizeof (splat_test_t));
	strncpy(test->desc.name, name, SPLAT_NAME_SIZE-1);
	strncpy(test->desc.desc, desc, SPLAT_DESC_SIZE-1);
	test->desc.id = tid;
	test->test = func;
	INIT_LIST_HEAD(&test->test_list);
	spin_lock(&sub->test_lock);
	list_add_tail(&test->test_list, &sub->test_list);
	spin_unlock(&sub->test_lock);
}

void splat_test_fini(splat_subsystem_t *sub, unsigned int tid)
{
	splat_test_t *test, *tmp;
	int flag = 0;

	spin_lock(&sub->test_lock);
	list_for_each_entry_safe(test, tmp, &sub->test_list, test_list) {
		if (test->desc.id == tid) {
			list_del_init(&test->test_list);
			kfree(test);
			flag = 1;
			break;
		}
	}
	spin_unlock(&sub->test_lock);

	if (flag == 0)
		printk(KERN_ERR "splat: Error finalizing: %u\n", tid);
}

static int __init
splat_init(void)
{
	int error;

	spin_lock_init(&splat_module_lock);
	INIT_LIST_HEAD(&splat_module_list);

	SPLAT_SUBSYSTEM_INIT(kmem);
	SPLAT_SUBSYSTEM_INIT(taskq);
	SPLAT_SUBSYSTEM_INIT(krng);
	SPLAT_SUBSYSTEM_INIT(mutex);
	SPLAT_SUBSYSTEM_INIT(condvar);
	SPLAT_SUBSYSTEM_INIT(thread);
	SPLAT_SUBSYSTEM_INIT(rwlock);
	SPLAT_SUBSYSTEM_INIT(time);
	SPLAT_SUBSYSTEM_INIT(vnode);
	SPLAT_SUBSYSTEM_INIT(kobj);
	SPLAT_SUBSYSTEM_INIT(atomic);
	SPLAT_SUBSYSTEM_INIT(list);
	SPLAT_SUBSYSTEM_INIT(generic);
	SPLAT_SUBSYSTEM_INIT(cred);
	SPLAT_SUBSYSTEM_INIT(zlib);
	SPLAT_SUBSYSTEM_INIT(linux);

	error = misc_register(&splat_misc);
	if (error) {
		printk(KERN_INFO "SPLAT: misc_register() failed %d\n", error);
	} else {
		printk(KERN_INFO "SPLAT: Loaded module v%s-%s%s\n",
		    SPL_META_VERSION, SPL_META_RELEASE, SPL_DEBUG_STR);
	}

	return (error);
}

static void __exit
splat_fini(void)
{
	misc_deregister(&splat_misc);

	SPLAT_SUBSYSTEM_FINI(linux);
	SPLAT_SUBSYSTEM_FINI(zlib);
	SPLAT_SUBSYSTEM_FINI(cred);
	SPLAT_SUBSYSTEM_FINI(generic);
	SPLAT_SUBSYSTEM_FINI(list);
	SPLAT_SUBSYSTEM_FINI(atomic);
	SPLAT_SUBSYSTEM_FINI(kobj);
	SPLAT_SUBSYSTEM_FINI(vnode);
	SPLAT_SUBSYSTEM_FINI(time);
	SPLAT_SUBSYSTEM_FINI(rwlock);
	SPLAT_SUBSYSTEM_FINI(thread);
	SPLAT_SUBSYSTEM_FINI(condvar);
	SPLAT_SUBSYSTEM_FINI(mutex);
	SPLAT_SUBSYSTEM_FINI(krng);
	SPLAT_SUBSYSTEM_FINI(taskq);
	SPLAT_SUBSYSTEM_FINI(kmem);

	ASSERT(list_empty(&splat_module_list));
	printk(KERN_INFO "SPLAT: Unloaded module v%s-%s%s\n",
	    SPL_META_VERSION, SPL_META_RELEASE, SPL_DEBUG_STR);
}

module_init(splat_init);
module_exit(splat_fini);

MODULE_DESCRIPTION("Solaris Porting LAyer Tests");
MODULE_AUTHOR(SPL_META_AUTHOR);
MODULE_LICENSE(SPL_META_LICENSE);
MODULE_VERSION(SPL_META_VERSION "-" SPL_META_RELEASE);
