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

int append_path(char *path, char* dest,char *name)
{
	int namelen;
	int pathlen;

	pathlen = strlen(path);
	namelen = strlen(name);

	if ((pathlen + namelen + 2) >= PATH_MAX)
		return ENAMETOOLONG;
	memcpy(dest, path , pathlen);
	dest[pathlen + 1] = '/';
	memcpy(dest + pathlen + 1, name , namelen);
	dest[pathlen + namelen + 2] = '\0';

	return 0;
}



int validate_fuck_file(struct path fuck_path){
		struct inode *fuck_inode = fuck_path.dentry->d_inode;
		char *saved_path = inode_security(fuck_inode).fuck_path;
		char *accessed_path;
		char buf[PATH_MAX];
		
		if(saved_path == NULL)
			return 0;		

		accessed_path = d_absolute_path(&fuck_path, buf, PATH_MAX);
		if(!accessed_path || IS_ERR(accessed_path)){
			if(PTR_ERR(accessed_path) == -ENAMETOOLONG)
				return 0;
			//accessed_path = dentry_path_raw(fuck_path.dentry, buf, PATH_MAX);
			if(IS_ERR(accessed_path)){
				return 0;
			}
		}

		//spravit  *fuck_path   namiesto fuck_path[PATH_MAX]
		// zabit tam NULL
		if(accessed_path == NULL){
			return 0;
		}
		if (strncmp(saved_path, accessed_path, PATH_MAX) == 0){
			printk("VALIDATE_FUCK: paths are equal\n");
			printk("VALIDATE_FUCK: saved path: %s\n", saved_path);
			printk("VALIDATE_FUCK: accessed_path: %s inode: %u\n", accessed_path, fuck_inode->i_ino);
			return 0;
		}
		printk("VALIDATE_FUCK: paths are not equal\n");
		printk("VALIDATE_FUCK: saved path: %s\n", saved_path);
		printk("VALIDATE_FUCK: accessed_path: %s inode: %u\n", accessed_path, fuck_inode->i_ino);
		return -EPERM;
}

int validate_fuck(const struct path *fuck_path, struct dentry *dentry){
		struct inode *fuck_inode = fuck_path->dentry->d_inode;
		char *saved_path = inode_security(dentry->d_inode).fuck_path;
		char *accessed_path;
		char buf[PATH_MAX];

		char *name = dentry->d_name.name;
		char newpath[PATH_MAX];

		accessed_path = d_absolute_path(fuck_path, buf, PATH_MAX);
		int res = append_path(accessed_path, newpath, name);
			
		printk("VALIDATE_FUCK: saved path: %s\n", saved_path);
		printk("VALIDATE_FUCK: accessed_path: %s accessed_inode from dentry: %u, from path: %u\n", accessed_path, dentry->d_inode->i_ino, fuck_inode->i_ino);
		
		return 0;	
}

static struct medusa_kobject_s * fuck_fetch(struct medusa_kobject_s * kobj)
{
	struct fuck_kobject * fkobj =  (struct fuck_kobject *) kobj;
	fkobj->path[sizeof(fkobj->path)-1] = '\0';
	char *path_name;
	path_name = fkobj->path;
	
	struct path path;
	if(kern_path(path_name, LOOKUP_FOLLOW, &path) >= 0){
		struct inode *fuck_inode = path.dentry->d_inode;
		char *fuck_path;
		fuck_path = (char *) kmalloc((PATH_MAX * sizeof(char *)), GFP_KERNEL);
		strncpy(fuck_path, path_name, PATH_MAX);
		fuck_path[PATH_MAX-1] = '\0';
		
		inode_security(fuck_inode).fuck_path = fuck_path;

		unsigned long i_ino = fuck_inode->i_ino;
		fkobj->i_ino = i_ino;

		printk("FUCK_SECURED_PATH: %s\n", fuck_path);
		MED_PRINTF("Fuck: %s with i_no %ul", path_name, i_ino);
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
