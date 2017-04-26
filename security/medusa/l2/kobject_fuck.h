/* kobject_fuck.c, (C) 2002 Milan Pikula */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/medusa/l3/registry.h>

struct fuck_kobject {	
	MEDUSA_KOBJECT_HEADER;
	char path[PATH_MAX];
	unsigned long i_ino;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(fuck_kobject);

