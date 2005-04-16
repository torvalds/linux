/*
 *  ioctl.c
 *
 *  Copyright (C) 1995, 1996 by Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/highuid.h>
#include <linux/net.h>

#include <linux/smb_fs.h>
#include <linux/smb_mount.h>

#include <asm/uaccess.h>

#include "proto.h"

int
smb_ioctl(struct inode *inode, struct file *filp,
	  unsigned int cmd, unsigned long arg)
{
	struct smb_sb_info *server = server_from_inode(inode);
	struct smb_conn_opt opt;
	int result = -EINVAL;

	switch (cmd) {
		uid16_t uid16;
		uid_t uid32;
	case SMB_IOC_GETMOUNTUID:
		SET_UID(uid16, server->mnt->mounted_uid);
		result = put_user(uid16, (uid16_t __user *) arg);
		break;
	case SMB_IOC_GETMOUNTUID32:
		SET_UID(uid32, server->mnt->mounted_uid);
		result = put_user(uid32, (uid_t __user *) arg);
		break;

	case SMB_IOC_NEWCONN:
		/* arg is smb_conn_opt, or NULL if no connection was made */
		if (!arg) {
			result = 0;
			smb_lock_server(server);
			server->state = CONN_RETRIED;
			printk(KERN_ERR "Connection attempt failed!  [%d]\n",
			       server->conn_error);
			smbiod_flush(server);
			smb_unlock_server(server);
			break;
		}

		result = -EFAULT;
		if (!copy_from_user(&opt, (void __user *)arg, sizeof(opt)))
			result = smb_newconn(server, &opt);
		break;
	default:
		break;
	}

	return result;
}
