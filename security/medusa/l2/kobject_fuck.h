/* kobject_fuck.c, (C) 2002 Milan Pikula */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l1/inode.h>

#define inode_security(inode) (*(struct medusa_l1_inode_s*)(inode->i_security))

//int validate_fuck_link(struct dentry *old_dentry, const struct path *fuck_path, struct dentry *new_dentry);
int validate_fuck_link(struct dentry *old_dentry);
int validate_fuck(const struct path *fuck_path);
int fuck_free(struct medusa_l1_inode_s* med);


struct fuck_kobject {	
	MEDUSA_KOBJECT_HEADER;
	char path[PATH_MAX];    /* primary key in 'fetch' operation */
	unsigned long ino;      /* primary key in 'update' operation */
	unsigned int dev;       /* primary key in 'update' operation */
	char action[20];        /* type of operation 'update' ('append' or 'remove') */
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(fuck_kobject);

