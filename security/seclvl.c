/**
 * BSD Secure Levels LSM
 *
 * Maintainers:
 *	Michael A. Halcrow <mike@halcrow.us>
 *	Serge Hallyn <hallyn@cs.wm.edu>
 *
 * Copyright (c) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (c) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (c) 2002 International Business Machines <robb@austin.ibm.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/netlink.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/kobject.h>
#include <linux/crypto.h>
#include <asm/scatterlist.h>
#include <linux/gfp.h>
#include <linux/sysfs.h>

#define SHA1_DIGEST_SIZE 20

/**
 * Module parameter that defines the initial secure level.
 *
 * When built as a module, it defaults to seclvl 1, which is the
 * behavior of BSD secure levels.  Note that this default behavior
 * wrecks havoc on a machine when the seclvl module is compiled into
 * the kernel.	In that case, we default to seclvl 0.
 */
#ifdef CONFIG_SECURITY_SECLVL_MODULE
static int initlvl = 1;
#else
static int initlvl;
#endif
module_param(initlvl, int, 0);
MODULE_PARM_DESC(initlvl, "Initial secure level (defaults to 1)");

/* Module parameter that defines the verbosity level */
static int verbosity;
module_param(verbosity, int, 0);
MODULE_PARM_DESC(verbosity, "Initial verbosity level (0 or 1; defaults to "
		 "0, which is Quiet)");

/**
 * Optional password which can be passed in to bring seclvl to 0
 * (i.e., for halt/reboot).  Defaults to NULL (the passwd attribute
 * file will not be registered in sysfs).
 *
 * This gets converted to its SHA1 hash when stored.  It's probably
 * not a good idea to use this parameter when loading seclvl from a
 * script; use sha1_passwd instead.
 */

#define MAX_PASSWD_SIZE	32
static char passwd[MAX_PASSWD_SIZE];
module_param_string(passwd, passwd, sizeof(passwd), 0);
MODULE_PARM_DESC(passwd,
		 "Plaintext of password that sets seclvl=0 when written to "
		 "(sysfs mount point)/seclvl/passwd\n");

/**
 * SHA1 hashed version of the optional password which can be passed in
 * to bring seclvl to 0 (i.e., for halt/reboot).  Must be in
 * hexadecimal format (40 characters).	Defaults to NULL (the passwd
 * attribute file will not be registered in sysfs).
 *
 * Use the sha1sum utility to generate the SHA1 hash of a password:
 *
 * echo -n "secret" | sha1sum
 */
#define MAX_SHA1_PASSWD	41
static char sha1_passwd[MAX_SHA1_PASSWD];
module_param_string(sha1_passwd, sha1_passwd, sizeof(sha1_passwd), 0);
MODULE_PARM_DESC(sha1_passwd,
		 "SHA1 hash (40 hexadecimal characters) of password that "
		 "sets seclvl=0 when plaintext password is written to "
		 "(sysfs mount point)/seclvl/passwd\n");

static int hideHash = 1;
module_param(hideHash, int, 0);
MODULE_PARM_DESC(hideHash, "When set to 0, reading seclvl/passwd from sysfs "
		 "will return the SHA1-hashed value of the password that "
		 "lowers the secure level to 0.\n");

#define MY_NAME "seclvl"

/**
 * This time-limits log writes to one per second.
 */
#define seclvl_printk(verb, type, fmt, arg...)			\
	do {							\
		if (verbosity >= verb) {			\
			static unsigned long _prior;		\
			unsigned long _now = jiffies;		\
			if ((_now - _prior) > HZ) {		\
				printk(type "%s: %s: " fmt,	\
					MY_NAME, __FUNCTION__ ,	\
					## arg);		\
				_prior = _now;			\
			}					\
		}						\
	} while (0)

/**
 * The actual security level.  Ranges between -1 and 2 inclusive.
 */
static int seclvl;

/**
 * flag to keep track of how we were registered
 */
static int secondary;

/**
 * Verifies that the requested secure level is valid, given the current
 * secure level.
 */
static int seclvl_sanity(int reqlvl)
{
	if ((reqlvl < -1) || (reqlvl > 2)) {
		seclvl_printk(1, KERN_WARNING, "Attempt to set seclvl out of "
			      "range: [%d]\n", reqlvl);
		return -EINVAL;
	}
	if ((seclvl == 0) && (reqlvl == -1))
		return 0;
	if (reqlvl < seclvl) {
		seclvl_printk(1, KERN_WARNING, "Attempt to lower seclvl to "
			      "[%d]\n", reqlvl);
		return -EPERM;
	}
	return 0;
}

/**
 * security level advancement rules:
 *   Valid levels are -1 through 2, inclusive.
 *   From -1, stuck.  [ in case compiled into kernel ]
 *   From 0 or above, can only increment.
 */
static void do_seclvl_advance(void *data, u64 val)
{
	int ret;
	int newlvl = (int)val;

	ret = seclvl_sanity(newlvl);
	if (ret)
		return;

	if (newlvl > 2) {
		seclvl_printk(1, KERN_WARNING, "Cannot advance to seclvl "
			      "[%d]\n", newlvl);
		return;
	}
	if (seclvl == -1) {
		seclvl_printk(1, KERN_WARNING, "Not allowed to advance to "
			      "seclvl [%d]\n", seclvl);
		return;
	}
	seclvl = newlvl;  /* would it be more "correct" to set *data? */
	return;
}

static u64 seclvl_int_get(void *data)
{
	return *(int *)data;
}

DEFINE_SIMPLE_ATTRIBUTE(seclvl_file_ops, seclvl_int_get, do_seclvl_advance, "%lld\n");

static unsigned char hashedPassword[SHA1_DIGEST_SIZE];

/**
 * Converts a block of plaintext of into its SHA1 hashed value.
 *
 * It would be nice if crypto had a wrapper to do this for us linear
 * people...
 */
static int
plaintext_to_sha1(unsigned char *hash, const char *plaintext, int len)
{
	char *pgVirtAddr;
	struct crypto_tfm *tfm;
	struct scatterlist sg[1];
	if (len > PAGE_SIZE) {
		seclvl_printk(0, KERN_ERR, "Plaintext password too large (%d "
			      "characters).  Largest possible is %lu "
			      "bytes.\n", len, PAGE_SIZE);
		return -ENOMEM;
	}
	tfm = crypto_alloc_tfm("sha1", CRYPTO_TFM_REQ_MAY_SLEEP);
	if (tfm == NULL) {
		seclvl_printk(0, KERN_ERR,
			      "Failed to load transform for SHA1\n");
		return -ENOSYS;
	}
	// Just get a new page; don't play around with page boundaries
	// and scatterlists.
	pgVirtAddr = (char *)__get_free_page(GFP_KERNEL);
	sg[0].page = virt_to_page(pgVirtAddr);
	sg[0].offset = 0;
	sg[0].length = len;
	strncpy(pgVirtAddr, plaintext, len);
	crypto_digest_init(tfm);
	crypto_digest_update(tfm, sg, 1);
	crypto_digest_final(tfm, hash);
	crypto_free_tfm(tfm);
	free_page((unsigned long)pgVirtAddr);
	return 0;
}

/**
 * Called whenever the user writes to the sysfs passwd handle to this kernel
 * object.  It hashes the password and compares the hashed results.
 */
static ssize_t
passwd_write_file(struct file * file, const char __user * buf,
				size_t count, loff_t *ppos)
{
	int i;
	unsigned char tmp[SHA1_DIGEST_SIZE];
	char *page;
	int rc;
	int len;

	if (!*passwd && !*sha1_passwd) {
		seclvl_printk(0, KERN_ERR, "Attempt to password-unlock the "
			      "seclvl module, but neither a plain text "
			      "password nor a SHA1 hashed password was "
			      "passed in as a module parameter!  This is a "
			      "bug, since it should not be possible to be in "
			      "this part of the module; please tell a "
			      "maintainer about this event.\n");
		return -EINVAL;
	}

	if (count < 0 || count >= PAGE_SIZE)
		return -EINVAL;
	if (*ppos != 0)
		return -EINVAL;
	page = (char *)get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	len = -EFAULT;
	if (copy_from_user(page, buf, count))
		goto out;
	
	len = strlen(page);
	/* ``echo "secret" > seclvl/passwd'' includes a newline */
	if (page[len - 1] == '\n')
		len--;
	/* Hash the password, then compare the hashed values */
	if ((rc = plaintext_to_sha1(tmp, page, len))) {
		seclvl_printk(0, KERN_ERR, "Error hashing password: rc = "
			      "[%d]\n", rc);
		return rc;
	}
	for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
		if (hashedPassword[i] != tmp[i])
			return -EPERM;
	}
	seclvl_printk(0, KERN_INFO,
		      "Password accepted; seclvl reduced to 0.\n");
	seclvl = 0;
	len = count;

out:
	free_page((unsigned long)page);
	return len;
}

static struct file_operations passwd_file_ops = {
	.write = passwd_write_file,
};

/**
 * Explicitely disallow ptrace'ing the init process.
 */
static int seclvl_ptrace(struct task_struct *parent, struct task_struct *child)
{
	if (seclvl >= 0) {
		if (child->pid == 1) {
			seclvl_printk(1, KERN_WARNING, "Attempt to ptrace "
				      "the init process dissallowed in "
				      "secure level %d\n", seclvl);
			return -EPERM;
		}
	}
	return 0;
}

/**
 * Capability checks for seclvl.  The majority of the policy
 * enforcement for seclvl takes place here.
 */
static int seclvl_capable(struct task_struct *tsk, int cap)
{
	/* init can do anything it wants */
	if (tsk->pid == 1)
		return 0;

	switch (seclvl) {
	case 2:
		/* fall through */
	case 1:
		if (cap == CAP_LINUX_IMMUTABLE) {
			seclvl_printk(1, KERN_WARNING, "Attempt to modify "
				      "the IMMUTABLE and/or APPEND extended "
				      "attribute on a file with the IMMUTABLE "
				      "and/or APPEND extended attribute set "
				      "denied in seclvl [%d]\n", seclvl);
			return -EPERM;
		} else if (cap == CAP_SYS_RAWIO) {	// Somewhat broad...
			seclvl_printk(1, KERN_WARNING, "Attempt to perform "
				      "raw I/O while in secure level [%d] "
				      "denied\n", seclvl);
			return -EPERM;
		} else if (cap == CAP_NET_ADMIN) {
			seclvl_printk(1, KERN_WARNING, "Attempt to perform "
				      "network administrative task while "
				      "in secure level [%d] denied\n", seclvl);
			return -EPERM;
		} else if (cap == CAP_SETUID) {
			seclvl_printk(1, KERN_WARNING, "Attempt to setuid "
				      "while in secure level [%d] denied\n",
				      seclvl);
			return -EPERM;
		} else if (cap == CAP_SETGID) {
			seclvl_printk(1, KERN_WARNING, "Attempt to setgid "
				      "while in secure level [%d] denied\n",
				      seclvl);
		} else if (cap == CAP_SYS_MODULE) {
			seclvl_printk(1, KERN_WARNING, "Attempt to perform "
				      "a module operation while in secure "
				      "level [%d] denied\n", seclvl);
			return -EPERM;
		}
		break;
	default:
		break;
	}
	/* from dummy.c */
	if (cap_is_fs_cap(cap) ? tsk->fsuid == 0 : tsk->euid == 0)
		return 0;	/* capability granted */
	seclvl_printk(1, KERN_WARNING, "Capability denied\n");
	return -EPERM;		/* capability denied */
}

/**
 * Disallow reversing the clock in seclvl > 1
 */
static int seclvl_settime(struct timespec *tv, struct timezone *tz)
{
	struct timespec now;
	if (seclvl > 1) {
		now = current_kernel_time();
		if (tv->tv_sec < now.tv_sec ||
		    (tv->tv_sec == now.tv_sec && tv->tv_nsec < now.tv_nsec)) {
			seclvl_printk(1, KERN_WARNING, "Attempt to decrement "
				      "time in secure level %d denied: "
				      "current->pid = [%d], "
				      "current->group_leader->pid = [%d]\n",
				      seclvl, current->pid,
				      current->group_leader->pid);
			return -EPERM;
		}		/* if attempt to decrement time */
	}			/* if seclvl > 1 */
	return 0;
}

/* claim the blockdev to exclude mounters, release on file close */
static int seclvl_bd_claim(struct inode *inode)
{
	int holder;
	struct block_device *bdev = NULL;
	dev_t dev = inode->i_rdev;
	bdev = open_by_devnum(dev, FMODE_WRITE);
	if (bdev) {
		if (bd_claim(bdev, &holder)) {
			blkdev_put(bdev);
			return -EPERM;
		}
		/* claimed, mark it to release on close */
		inode->i_security = current;
	}
	return 0;
}

/* release the blockdev if you claimed it */
static void seclvl_bd_release(struct inode *inode)
{
	if (inode && S_ISBLK(inode->i_mode) && inode->i_security == current) {
		struct block_device *bdev = inode->i_bdev;
		if (bdev) {
			bd_release(bdev);
			blkdev_put(bdev);
			inode->i_security = NULL;
		}
	}
}

/**
 * Security for writes to block devices is regulated by this seclvl
 * function.  Deny all writes to block devices in seclvl 2.  In
 * seclvl 1, we only deny writes to *mounted* block devices.
 */
static int
seclvl_inode_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	if (current->pid != 1 && S_ISBLK(inode->i_mode) && (mask & MAY_WRITE)) {
		switch (seclvl) {
		case 2:
			seclvl_printk(1, KERN_WARNING, "Write to block device "
				      "denied in secure level [%d]\n", seclvl);
			return -EPERM;
		case 1:
			if (seclvl_bd_claim(inode)) {
				seclvl_printk(1, KERN_WARNING,
					      "Write to mounted block device "
					      "denied in secure level [%d]\n",
					      seclvl);
				return -EPERM;
			}
		}
	}
	return 0;
}

/**
 * The SUID and SGID bits cannot be set in seclvl >= 1
 */
static int seclvl_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	if (seclvl > 0) {
		if (iattr->ia_valid & ATTR_MODE)
			if (iattr->ia_mode & S_ISUID ||
			    iattr->ia_mode & S_ISGID) {
				seclvl_printk(1, KERN_WARNING, "Attempt to "
					      "modify SUID or SGID bit "
					      "denied in seclvl [%d]\n",
					      seclvl);
				return -EPERM;
			}
	}
	return 0;
}

/* release busied block devices */
static void seclvl_file_free_security(struct file *filp)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *inode = NULL;

	if (dentry) {
		inode = dentry->d_inode;
		seclvl_bd_release(inode);
	}
}

/**
 * Cannot unmount in secure level 2
 */
static int seclvl_umount(struct vfsmount *mnt, int flags)
{
	if (current->pid == 1)
		return 0;
	if (seclvl == 2) {
		seclvl_printk(1, KERN_WARNING, "Attempt to unmount in secure "
			      "level %d\n", seclvl);
		return -EPERM;
	}
	return 0;
}

static struct security_operations seclvl_ops = {
	.ptrace = seclvl_ptrace,
	.capable = seclvl_capable,
	.inode_permission = seclvl_inode_permission,
	.inode_setattr = seclvl_inode_setattr,
	.file_free_security = seclvl_file_free_security,
	.settime = seclvl_settime,
	.sb_umount = seclvl_umount,
};

/**
 * Process the password-related module parameters
 */
static int processPassword(void)
{
	int rc = 0;
	hashedPassword[0] = '\0';
	if (*passwd) {
		if (*sha1_passwd) {
			seclvl_printk(0, KERN_ERR, "Error: Both "
				      "passwd and sha1_passwd "
				      "were set, but they are mutually "
				      "exclusive.\n");
			return -EINVAL;
		}
		if ((rc = plaintext_to_sha1(hashedPassword, passwd,
					    strlen(passwd)))) {
			seclvl_printk(0, KERN_ERR, "Error: SHA1 support not "
				      "in kernel\n");
			return rc;
		}
		/* All static data goes to the BSS, which zero's the
		 * plaintext password out for us. */
	} else if (*sha1_passwd) {	// Base 16
		int i;
		i = strlen(sha1_passwd);
		if (i != (SHA1_DIGEST_SIZE * 2)) {
			seclvl_printk(0, KERN_ERR, "Received [%d] bytes; "
				      "expected [%d] for the hexadecimal "
				      "representation of the SHA1 hash of "
				      "the password.\n",
				      i, (SHA1_DIGEST_SIZE * 2));
			return -EINVAL;
		}
		while ((i -= 2) + 2) {
			unsigned char tmp;
			tmp = sha1_passwd[i + 2];
			sha1_passwd[i + 2] = '\0';
			hashedPassword[i / 2] = (unsigned char)
			    simple_strtol(&sha1_passwd[i], NULL, 16);
			sha1_passwd[i + 2] = tmp;
		}
	}
	return 0;
}

/**
 * securityfs registrations
 */
struct dentry *dir_ino, *seclvl_ino, *passwd_ino;

static int seclvlfs_register(void)
{
	dir_ino = securityfs_create_dir("seclvl", NULL);
	if (!dir_ino)
		return -EFAULT;

	seclvl_ino = securityfs_create_file("seclvl", S_IRUGO | S_IWUSR,
				dir_ino, &seclvl, &seclvl_file_ops);
	if (!seclvl_ino)
		goto out_deldir;
	if (*passwd || *sha1_passwd) {
		passwd_ino = securityfs_create_file("passwd", S_IRUGO | S_IWUSR,
				dir_ino, NULL, &passwd_file_ops);
		if (!passwd_ino)
			goto out_delf;
	}
	return 0;

out_deldir:
	securityfs_remove(dir_ino);
out_delf:
	securityfs_remove(seclvl_ino);

	return -EFAULT;
}

/**
 * Initialize the seclvl module.
 */
static int __init seclvl_init(void)
{
	int rc = 0;
	if (verbosity < 0 || verbosity > 1) {
		printk(KERN_ERR "Error: bad verbosity [%d]; only 0 or 1 "
		       "are valid values\n", verbosity);
		rc = -EINVAL;
		goto exit;
	}
	if (initlvl < -1 || initlvl > 2) {
		seclvl_printk(0, KERN_ERR, "Error: bad initial securelevel "
			      "[%d].\n", initlvl);
		rc = -EINVAL;
		goto exit;
	}
	seclvl = initlvl;
	if ((rc = processPassword())) {
		seclvl_printk(0, KERN_ERR, "Error processing the password "
			      "module parameter(s): rc = [%d]\n", rc);
		goto exit;
	}
	/* register ourselves with the security framework */
	if (register_security(&seclvl_ops)) {
		seclvl_printk(0, KERN_ERR,
			      "seclvl: Failure registering with the "
			      "kernel.\n");
		/* try registering with primary module */
		rc = mod_reg_security(MY_NAME, &seclvl_ops);
		if (rc) {
			seclvl_printk(0, KERN_ERR, "seclvl: Failure "
				      "registering with primary security "
				      "module.\n");
			goto exit;
		}		/* if primary module registered */
		secondary = 1;
	}			/* if we registered ourselves with the security framework */
	if ((rc = seclvlfs_register())) {
		seclvl_printk(0, KERN_ERR, "Error registering with sysfs\n");
		goto exit;
	}
	seclvl_printk(0, KERN_INFO, "seclvl: Successfully initialized.\n");
 exit:
	if (rc) {
		printk(KERN_ERR "seclvl: Error during initialization: rc = "
		       "[%d]\n", rc);
	}
	return rc;
}

/**
 * Remove the seclvl module.
 */
static void __exit seclvl_exit(void)
{
	securityfs_remove(seclvl_ino);
	if (*passwd || *sha1_passwd)
		securityfs_remove(passwd_ino);
	securityfs_remove(dir_ino);
	if (secondary == 1) {
		mod_unreg_security(MY_NAME, &seclvl_ops);
	} else if (unregister_security(&seclvl_ops)) {
		seclvl_printk(0, KERN_INFO,
			      "seclvl: Failure unregistering with the "
			      "kernel\n");
	}
}

module_init(seclvl_init);
module_exit(seclvl_exit);

MODULE_AUTHOR("Michael A. Halcrow <mike@halcrow.us>");
MODULE_DESCRIPTION("LSM implementation of the BSD Secure Levels");
MODULE_LICENSE("GPL");
