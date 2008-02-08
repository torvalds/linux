/* MN10300 Kernel parameters
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_PARAM_H
#define _ASM_PARAM_H

#ifdef __KERNEL__
#define HZ		1000		/* Internal kernel timer frequency */
#define USER_HZ		100		/* .. some user interfaces are in
					 * "ticks" */
#define CLOCKS_PER_SEC	(USER_HZ)	/* like times() */
#endif

#ifndef HZ
#define HZ		100
#endif

#define EXEC_PAGESIZE	4096

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */
#define COMMAND_LINE_SIZE 256

#endif /* _ASM_PARAM_H */
