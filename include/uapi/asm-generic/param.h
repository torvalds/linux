/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ASM_GENERIC_PARAM_H
#define _UAPI__ASM_GENERIC_PARAM_H

#ifndef __USER_HZ
#define __USER_HZ	100
#endif

#ifndef HZ
#define HZ __USER_HZ
#endif

#ifndef EXEC_PAGESIZE
#define EXEC_PAGESIZE	4096
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */


#endif /* _UAPI__ASM_GENERIC_PARAM_H */
