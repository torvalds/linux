/* Public domain. */

#ifndef _LINUX_MODULEPARAM_H
#define _LINUX_MODULEPARAM_H

#define MODULE_PARM_DESC(parm, desc)
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define module_param_named_unsafe(name, value, type, perm)
#define module_param_unsafe(name, type, perm)
#define module_param_string(name, string, len, perm)

#endif
