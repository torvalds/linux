/*
 * AppArmor security module
 *
 * This file contains AppArmor basic permission sets definitions.
 *
 * Copyright 2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_PERM_H
#define __AA_PERM_H

#include <linux/fs.h>

/*
 * We use MAY_EXEC, MAY_WRITE, MAY_READ, MAY_APPEND and the following flags
 * for profile permissions
 */
#define AA_MAY_CREATE                  0x0010
#define AA_MAY_DELETE                  0x0020
#define AA_MAY_META_WRITE              0x0040
#define AA_MAY_META_READ               0x0080

#define AA_MAY_CHMOD                   0x0100
#define AA_MAY_CHOWN                   0x0200
#define AA_MAY_LOCK                    0x0400
#define AA_EXEC_MMAP                   0x0800

#define AA_MAY_LINK			0x1000
#define AA_LINK_SUBSET			AA_MAY_LOCK	/* overlaid */
#define AA_MAY_ONEXEC			0x40000000	/* exec allows onexec */
#define AA_MAY_CHANGE_PROFILE		0x80000000
#define AA_MAY_CHANGEHAT		0x80000000	/* ctrl auditing only */


#endif /* __AA_PERM_H */
