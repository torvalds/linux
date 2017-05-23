/* kobject_fuck.c, (C) 2002 Milan Pikula */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/medusa/l3/registry.h>
#include "kobject_fuck.h"
MED_ATTRS(fuck_kobject) {
	MED_ATTR_KEY	(fuck_kobject, path, "path", MED_STRING),
	MED_ATTR		(fuck_kobject, i_ino, "i_ino", MED_UNSIGNED),
	MED_ATTR_OBJECT (fuck_kobject),
	MED_ATTR_END
};

//append name to path and save it to dest
//not used
int append_path(char *path, char* dest, const unsigned char *name)
{
	int namelen;
	int pathlen;

	pathlen = strlen(path);
	namelen = strlen(name);

	if ((pathlen + namelen + 2) > PATH_MAX)
		return ENAMETOOLONG;
	memcpy(dest, path , pathlen);
	dest[pathlen] = '/';
	memcpy(dest + pathlen + 1, name , namelen);
	dest[pathlen + namelen + 1] = '\0';
	
	return 0;
}


//used in medusa_l1_path_chown, medusa_l1_path_chmod, medusa_l1_file_open
int validate_fuck(struct path fuck_path){
		struct inode *fuck_inode = fuck_path.dentry->d_inode;
		char *saved_path = inode_security(fuck_inode).fuck_path;
		char *accessed_path;
		char *buf;
		
		//dont change to goto out you dont have buf
		if(saved_path == NULL)
			return 0;	
		
		buf = (char *) kmalloc(PATH_MAX * sizeof(char), GFP_KERNEL);
		if(!buf)
			goto out;

		accessed_path = d_absolute_path(&fuck_path, buf, PATH_MAX);
		if(!accessed_path || IS_ERR(accessed_path)){
			if(PTR_ERR(accessed_path) == -ENAMETOOLONG)
				goto out;
			//accessed_path = dentry_path_raw(fuck_path.dentry, buf, PATH_MAX);
			if(IS_ERR(accessed_path)){
				goto out;
			}
		}

		if(accessed_path == NULL){
			goto out;
		}
		if (strncmp(saved_path, accessed_path, PATH_MAX) == 0){
			printk("VALIDATE_FUCK: paths are equal\n");
			printk("VALIDATE_FUCK: saved path: %s\n", saved_path);
			printk("VALIDATE_FUCK: accessed_path: %s inode: %lu\n", accessed_path, fuck_inode->i_ino);
			goto out;
		}
		printk("VALIDATE_FUCK: paths are not equal\n");
		printk("VALIDATE_FUCK: saved path: %s\n", saved_path);
		printk("VALIDATE_FUCK: accessed_path: %s inode: %lu\n", accessed_path, fuck_inode->i_ino);
		kfree(buf);
		return -EPERM;
out:
		kfree(buf);
		return 0;
}

//used in medusa_l1_path_link
//int validate_fuck_link(struct dentry *old_dentry, const struct path *fuck_path, struct dentry *new_dentry){
int validate_fuck_link(struct dentry *old_dentry){
		struct inode *fuck_inode = old_dentry->d_inode;
		char *saved_path = inode_security(fuck_inode).fuck_path;
		//if inode has no protected paths defined, allow hard link alse deny
		if(saved_path == NULL) 
			  return 0;
		return -EPERM;
		/*
		char *accessed_path;
		char *parent_path;
		char *buf;
		const unsigned char *d_name = new_dentry->d_name.name;

		//dont change to goto out you dont have buf
		if(saved_path == NULL)
			return 0;
		buf = (char *) kmalloc(PATH_MAX * sizeof(char), GFP_KERNEL);
		accessed_path = (char *) kmalloc(PATH_MAX * sizeof(char), GFP_KERNEL);
		if(!buf || !accessed_path)
			goto out;

		parent_path = d_absolute_path(fuck_path, buf, PATH_MAX);
		if(!parent_path || IS_ERR(parent_path)){
			if(PTR_ERR(parent_path) == -ENAMETOOLONG)
				goto out;
			if(IS_ERR(parent_path)){
				goto out;
			}
		}
		
		if(append_path(parent_path, accessed_path, d_name) != 0)
			goto out;

		if(accessed_path == NULL){
			goto out;
		}
		if (strncmp(saved_path, accessed_path, PATH_MAX) == 0){
			printk("VALIDATE_FUCK_LINK: paths are equal\n");
			printk("VALIDATE_FUCK_LINK: saved path: %s\n", saved_path);
			printk("VALIDATE_FUCK_LINK: accessed_path: %s inode: %lu\n", accessed_path, fuck_inode->i_ino);
			goto out;
		}
		printk("VALIDATE_FUCK_LINK: paths are not equal\n");
		printk("VALIDATE_FUCK_LINK: saved path: %s\n", saved_path);
		printk("VALIDATE_FUCK_LINK: accessed_path: %s inode: %lu\n", accessed_path, fuck_inode->i_ino);
		kfree(buf);
		kfree(accessed_path);
		return -EPERM;
out:
		kfree(buf);
		kfree(accessed_path);
		return 0;
	*/
}

static struct medusa_kobject_s * fuck_fetch(struct medusa_kobject_s * kobj)
{
	struct fuck_kobject * fkobj =  (struct fuck_kobject *) kobj;
	char *path_name;
	struct path path;
	unsigned long i_ino; 
	fkobj->path[sizeof(fkobj->path)-1] = '\0';
	path_name = fkobj->path;

	if(kern_path(path_name, LOOKUP_FOLLOW, &path) >= 0){
		struct inode *fuck_inode = path.dentry->d_inode;
		
		char *fuck_path = (char *) kmalloc((PATH_MAX * sizeof(char)), GFP_KERNEL);
		strncpy(fuck_path, path_name, PATH_MAX);
		fuck_path[PATH_MAX-1] = '\0';
		
		inode_security(fuck_inode).fuck_path = fuck_path;

		i_ino = fuck_inode->i_ino;
		fkobj->i_ino = i_ino;

		printk("FUCK_SECURED_PATH: %s\n", fuck_path);
		MED_PRINTF("Fuck: %s with i_no %lu", path_name, i_ino);
	}else{
		MED_PRINTF("Fuck: %s have no inode", path_name);
	}
	
	return (struct medusa_kobject_s *)fkobj;
}

static medusa_answer_t fuck_update(struct medusa_kobject_s * kobj)
{
	return MED_OK;
}

MED_KCLASS(fuck_kobject) {
	MEDUSA_KCLASS_HEADER(fuck_kobject),
	"fuck",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	fuck_fetch,
	fuck_update,
	NULL,		/* unmonitor */
};

void fuck_kobject_rmmod(void);

int __init fuck_kobject_init(void) {
	MED_REGISTER_KCLASS(fuck_kobject);
	return 0;
}

__initcall(fuck_kobject_init);
