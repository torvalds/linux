/* Public domain. */

#ifndef _LINUX_MODULE_H
#define _LINUX_MODULE_H

#include <linux/export.h>
#include <linux/moduleparam.h>
#include <linux/kobject.h>

struct module;

#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x)
#define MODULE_LICENSE(x)
#define MODULE_FIRMWARE(x)
#define MODULE_DEVICE_TABLE(x, y)
#define MODULE_IMPORT_NS(x)
#define module_init(x)
#define module_exit(x)
#define symbol_put(x)

static inline bool
try_module_get(struct module *m)
{
	return true;
}

static inline void
module_put(struct module *m)
{
}

#endif
