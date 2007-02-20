/*
 * Pioctl operations for Coda.
 * Original version: (C) 1996 Peter Braam 
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>

/* pioctl ops */
static int coda_ioctl_permission(struct inode *inode, int mask,
				 struct nameidata *nd);
static int coda_pioctl(struct inode * inode, struct file * filp, 
                       unsigned int cmd, unsigned long user_data);

/* exported from this file */
const struct inode_operations coda_ioctl_inode_operations =
{
	.permission	= coda_ioctl_permission,
	.setattr	= coda_setattr,
};

const struct file_operations coda_ioctl_operations = {
	.owner		= THIS_MODULE,
	.ioctl		= coda_pioctl,
};

/* the coda pioctl inode ops */
static int coda_ioctl_permission(struct inode *inode, int mask,
				 struct nameidata *nd)
{
        return 0;
}

static int coda_pioctl(struct inode * inode, struct file * filp, 
                       unsigned int cmd, unsigned long user_data)
{
	struct nameidata nd;
        int error;
	struct PioctlData data;
        struct inode *target_inode = NULL;
        struct coda_inode_info *cnp;

        /* get the Pioctl data arguments from user space */
        if (copy_from_user(&data, (void __user *)user_data, sizeof(data))) {
	    return -EINVAL;
	}
       
        /* 
         * Look up the pathname. Note that the pathname is in 
         * user memory, and namei takes care of this
         */
        if ( data.follow ) {
                error = user_path_walk(data.path, &nd);
	} else {
	        error = user_path_walk_link(data.path, &nd);
	}
		
	if ( error ) {
		return error;
        } else {
	        target_inode = nd.dentry->d_inode;
	}
	
	/* return if it is not a Coda inode */
	if ( target_inode->i_sb != inode->i_sb ) {
		path_release(&nd);
	        return  -EINVAL;
	}

	/* now proceed to make the upcall */
        cnp = ITOC(target_inode);

	error = venus_pioctl(inode->i_sb, &(cnp->c_fid), cmd, &data);

	path_release(&nd);
        return error;
}

