/* kobject_fuck.c, (C) 2002 Milan Pikula */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/medusa/l3/registry.h>
#include "kobject_fuck.h"


MED_ATTRS(fuck_kobject) {
	MED_ATTR_KEY	(fuck_kobject, path, "path", MED_STRING),
	MED_ATTR		(fuck_kobject, i_ino, "i_ino", MED_UNSIGNED),
	MED_ATTR_OBJECT (fuck_kobject),
	MED_ATTR_END
};

static struct medusa_kobject_s * fuck_fetch(struct medusa_kobject_s * kobj)
{
	struct fuck_kobject * fkobj =  (struct fuck_kobject *) kobj;
	fkobj->path[sizeof(fkobj->path)-1] = '\0';
	char *path_name;
	path_name = fkobj->path;
	
	struct path path;
	if(kern_path(path_name, LOOKUP_FOLLOW, &path) >= 0){
		struct inode *fuck_inode = path.dentry->d_inode;
		struct medusa_l1_inode_s *fuck_security = &inode_security(fuck_inode); 
		strncpy(fuck_security->med_object.fuck_path, path_name, PATH_MAX);
		unsigned long i_ino = fuck_inode->i_ino;
		fkobj->i_ino = i_ino;
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
