#ifndef _TOOLS_INCLUDE_LINUX_LINKAGE_H
#define _TOOLS_INCLUDE_LINUX_LINKAGE_H

#include <linux/export.h>

#define SYM_FUNC_START(x) .globl x; x:

#define SYM_FUNC_END(x)

#endif /* _TOOLS_INCLUDE_LINUX_LINKAGE_H */
