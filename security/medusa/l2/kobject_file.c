/* file_kobject.c, (C) 2002 Milan Pikula */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/inode.h>

#include "kobject_file.h"

int file_kobj2kern(struct file_kobject * fk, struct inode * inode)
{
	/* TODO: either update the i-node on disk, or don't allow this at all */
	inode->i_mode = fk->mode;
	inode->i_uid = fk->uid;
	inode->i_gid = fk->gid;
	COPY_MEDUSA_OBJECT_VARS(&inode_security(inode), fk);
	inode_security(inode).user = fk->user;
#ifdef CONFIG_MEDUSA_FILE_CAPABILITIES
	inode_security(inode).ecap = fk->ecap;
	inode_security(inode).icap = fk->icap;
	inode_security(inode).pcap = fk->pcap;
#endif /* CONFIG_MEDUSA_FILE_CAPABILITIES */
	MED_MAGIC_VALIDATE(&inode_security(inode));
	return 0;
}

int file_kern2kobj(struct file_kobject * fk, struct inode * inode)
{
	fk->dev = (inode->i_sb->s_dev);
	fk->ino = inode->i_ino;
	fk->mode = inode->i_mode;
	fk->nlink = inode->i_nlink;
	fk->uid = inode->i_uid;
	fk->gid = inode->i_gid;
	fk->rdev = (inode->i_rdev);
	COPY_MEDUSA_OBJECT_VARS(fk, &inode_security(inode));
	fk->user = inode_security(inode).user;
#ifdef CONFIG_MEDUSA_FILE_CAPABILITIES
	fk->ecap = inode_security(inode).ecap;
	fk->icap = inode_security(inode).icap;
	fk->pcap = inode_security(inode).pcap;
#endif /* CONFIG_MEDUSA_FILE_CAPABILITIES */
	return 0;
}

/* second, we will describe its attributes, and provide fetch and update
 * routines */
/* (that's for l4, they will be working with those descriptions) */

MED_ATTRS(file_kobject) {
	MED_ATTR_KEY_RO	(file_kobject, dev, "dev", MED_UNSIGNED),
	MED_ATTR_KEY_RO	(file_kobject, ino, "ino", MED_UNSIGNED),
	MED_ATTR	(file_kobject, mode, "mode", MED_BITMAP),
	MED_ATTR_RO	(file_kobject, nlink, "nlink", MED_UNSIGNED),
	MED_ATTR	(file_kobject, uid, "uid", MED_UNSIGNED),
	MED_ATTR	(file_kobject, gid, "gid", MED_UNSIGNED),
	MED_ATTR_RO	(file_kobject, rdev, "rdev", MED_UNSIGNED),
	MED_ATTR_OBJECT	(file_kobject),
	MED_ATTR	(file_kobject, user, "user", MED_UNSIGNED),
#ifdef CONFIG_MEDUSA_FILE_CAPABILITIES
	MED_ATTR	(file_kobject, ecap, "ecap", MED_BITMAP),
	MED_ATTR	(file_kobject, icap, "icap", MED_BITMAP),
	MED_ATTR	(file_kobject, pcap, "pcap", MED_BITMAP),
#endif
	MED_ATTR_END
};

/* here are the inodes, which are currently being examined by the L4
 * code. This simplifies a lookup, and at the moment it is also the only
 * way for L4 to fetch or update a i-node.
 */
static DEFINE_RWLOCK(live_lock);
 static struct inode * live_inodes = NULL;

/* TODO: if it shows there are many concurrent inodes in the list,
 * rewrite this to use in-kernel hashes; if there will be a FAST global
 * lookup routine, maybe we can delete this at all.
 *
 * Note that we don't modify inode ref_count: call this only with
 * locked i-node.
 */
void file_kobj_live_add(struct inode * ino)
{
	struct inode * tmp;

	write_lock(&live_lock);
	for (tmp = live_inodes; tmp; tmp = inode_security(tmp).next_live)
		if (tmp == ino) {
			inode_security(tmp).use_count++;
			write_unlock(&live_lock);
			return;
		}
	inode_security(ino).next_live = live_inodes;
	inode_security(ino).use_count = 1;
	live_inodes = ino;
	write_unlock(&live_lock);
}
void file_kobj_live_remove(struct inode * ino)
{
	struct inode * tmp;

	write_lock(&live_lock);
	if (--inode_security(ino).use_count)
		goto out;
	if (ino == live_inodes) {
		live_inodes = inode_security(ino).next_live;
		write_unlock(&live_lock);
		return;
	}
	for (tmp = live_inodes; inode_security(tmp).next_live; tmp = inode_security(tmp).next_live)
		if (inode_security(tmp).next_live == ino) {
			inode_security(tmp).next_live = inode_security(ino).next_live;
			break;
		}
out:
	write_unlock(&live_lock);
}
void file_kobj_dentry2string(struct dentry * dentry, char * buf)
{
	int len;

	if( IS_ROOT(dentry) )
	{
		struct path ndcurrent, ndupper;
		
		ndcurrent.dentry = dentry;
		ndcurrent.mnt = NULL;
		medusa_get_upper_and_parent(&ndcurrent,&ndupper,NULL);
		dentry=dget(ndupper.dentry);
		medusa_put_upper_and_parent(&ndupper, NULL);
	}
	else
		dget(dentry);
		
	if (!dentry || IS_ERR(dentry) || !dentry->d_name.name) {
		buf[0] = '\0';
		dput(dentry);
		return;
	}
	len = dentry->d_name.len < NAME_MAX ?
		dentry->d_name.len : NAME_MAX;
	memcpy(buf, dentry->d_name.name, len);
	buf[len] = '\0';
	dput(dentry);
}

static struct file_kobject storage;

static inline struct inode * __lookup_inode_by_key(struct file_kobject * key_obj)
{
	struct inode * p;

	read_lock(&live_lock);
	for (p = live_inodes; p; p = inode_security(p).next_live)
		if (p->i_ino == key_obj->ino)
			if (p->i_sb->s_dev == key_obj->dev)
				break;

	return p;
}

static inline void __unlookup(void)
{
	read_unlock(&live_lock);
}

static struct medusa_kobject_s * file_fetch(struct medusa_kobject_s * key_obj)
{
	struct inode * p;

	p = __lookup_inode_by_key((struct file_kobject *)key_obj);
	if (p) {
		file_kern2kobj(&storage, p);
		__unlookup();
		return (struct medusa_kobject_s *)&storage;
	}
	__unlookup();
	return NULL;
}

static void file_unmonitor(struct medusa_kobject_s * kobj)
{
	struct inode * p;

	p = __lookup_inode_by_key((struct file_kobject *)kobj);
	if (p) {
		UNMONITOR_MEDUSA_OBJECT_VARS(&inode_security(p));
		MED_MAGIC_VALIDATE(&inode_security(p));
	}
	__unlookup();
}

static medusa_answer_t file_update(struct medusa_kobject_s * kobj)
{
	struct inode * p;
	medusa_answer_t retval = MED_ERR;

	p = __lookup_inode_by_key((struct file_kobject *)kobj);
	if (p) {
		file_kobj2kern((struct file_kobject *)kobj, p);
		retval = MED_OK;
	}
	__unlookup();
	return retval;
}

/* third, we will define the kclass, describing such objects */
/* that's for L3, to make it happy */

MED_KCLASS(file_kobject) {
	MEDUSA_KCLASS_HEADER(file_kobject),
	"file",

	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	file_fetch,	/* fetch kobject */
	file_update,	/* update kobject */
	file_unmonitor,	/* disable all monitoring on kobj. */
};

int __init file_kobject_init(void) {
	MED_REGISTER_KCLASS(file_kobject);
	return 0;
}

__initcall(file_kobject_init);
