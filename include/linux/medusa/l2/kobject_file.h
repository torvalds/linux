/* inode_kobject.h, (C) 2002 Milan Pikula
 *
 * FILE kobject: this file defines the kobject structure for inode, e.g.
 * the data, which we want to pass to the authorization server.
 *
 * The structure contains some data from ordinary struct inode,
 * and some data from medusa_l1_inode_s, which is defined in
 * medusa/l1/inode.h.
 *
 * This file (as well as many others) is based on Medusa DS9, version
 * 0.9.2, which is (C) Marek Zelem, Martin Ockajak and myself.
 */

#ifndef _INODE_KOBJECT_H
#define _INODE_KOBJECT_H

//#include <medusa/l1/inode.h>
#include <linux/fs.h>		/* contains all includes we need ;) */
#include <linux/medusa/l3/kobject.h>

#pragma GCC optimize ("Og")

struct file_kobject { /* was: m_inode_inf */
	MEDUSA_KOBJECT_HEADER;
/*
 * As a preparation for the total deletion of device numbers,
 * we introduce a type unsigned long to hold them. No information about
 * this type is known outside of this include file.
 * 
 * ... for more folklore read the comment in kdev_t.h ;)
 */
	unsigned long dev;
	unsigned long ino;

	umode_t mode;
	nlink_t nlink;
	uid_t uid;
	gid_t gid;
	unsigned long rdev;
	
	MEDUSA_OBJECT_VARS;

	__u32 user;
#ifdef CONFIG_MEDUSA_FILE_CAPABILITIES
	kernel_cap_t icap;	/* support for Linux capabilities */
	kernel_cap_t pcap; 
	kernel_cap_t ecap;
#endif /* CONFIG_MEDUSA_FILE_CAPABILITIES */
};
extern MED_DECLARE_KCLASSOF(file_kobject);

struct file_sub_kobject { /* the 'subject' view... */
	struct file_kobject f;
	MEDUSA_SUBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(file_sub_kobject);

/* the conversion routines */
int file_kobj2kern(struct file_kobject * fk, struct inode * inode);
int file_kern2kobj(struct file_kobject * fk, struct inode * inode);

/* we want to keep a cache of "live" inodes - the ones which participate
 * on some access right now
 */
void file_kobj_live_add(struct inode * ino);
void file_kobj_live_remove(struct inode * ino);

/* conversion beteween filename (stored in dentry) and static buffer */
void file_kobj_dentry2string(struct dentry * dentry, char * buf);

#endif
