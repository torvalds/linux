/* (C) 2002 Milan Pikula */
#include <linux/medusa/l3/registry.h>
#include <linux/dcache.h>
#include <linux/fs_struct.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include "../../../fs/mount.h"

#include "kobject_fuck.h"
#include <linux/medusa/l1/file_handlers.h>

/* the getfuck event types (yes, there are more of them) are a bit special:
 * 1) they are called from the beginning of various access types to get the
 *    initial VS set,
 * 2) they gain some additional information, which enables L4 code to keep
 *    the file hierarchy, if it wants.
 * 3) due to creepy VFS design in Linux we sometimes do some magic.
 */

struct getfuck_event {
	MEDUSA_ACCESS_HEADER;
	char path[NAME_MAX+1];
};

MED_ATTRS(getfuck_event) {
	MED_ATTR_RO (getfuck_event, path, "path", MED_STRING),
	MED_ATTR_END
};
MED_EVTYPE(getfuck_event, "getfuck", fuck_kobject, "fuck",
		fuck_kobject, "parent");

int validate_fuck(struct path* path){
	struct medusa_l1_inode_s *fuck_security = &inode_security(path->dentry->d_inode);	
	char *fuck_path = fuck_security->med_object.fuck_path;
	if(!fuck_path){
		printk("FUCK_VALIDATE: No fuck paths\n");
		return 0;
	}
	
	char path_buf[PATH_MAX];
	char *path_real;
	int error = 0;

	path_real = d_absolute_path(path, path_buf, PATH_MAX);
	if(!path_real || IS_ERR(path_real)){
		if(PTR_ERR(path_real) == -ENAMETOOLONG)
			return -ENAMETOOLONG;
		path_real = dentry_path_raw(path->dentry, path_buf, PATH_MAX);
		if(IS_ERR(path_real)){
			error = PTR_ERR(path_real);
			return error;
		}
	}
	
	if(strcmp(path_real, fuck_path) == 0) {
		printk("FUCK_VALIDATE: It is equal\n");
		return 0;
	}
	return 1;
}

int __init getfuck_evtype_init(void) {
	MED_REGISTER_EVTYPE(getfuck_event,
			MEDUSA_EVTYPE_TRIGGEREDBYOBJECTTBIT);
	return 0;
}
__initcall(getfuck_evtype_init);





