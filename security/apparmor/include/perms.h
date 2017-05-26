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

#define AA_MAY_EXEC		MAY_EXEC
#define AA_MAY_WRITE		MAY_WRITE
#define AA_MAY_READ		MAY_READ
#define AA_MAY_APPEND		MAY_APPEND

#define AA_MAY_CREATE		0x0010
#define AA_MAY_DELETE		0x0020
#define AA_MAY_OPEN		0x0040
#define AA_MAY_RENAME		0x0080		/* pair */

#define AA_MAY_SETATTR		0x0100		/* meta write */
#define AA_MAY_GETATTR		0x0200		/* meta read */
#define AA_MAY_SETCRED		0x0400		/* security cred/attr */
#define AA_MAY_GETCRED		0x0800

#define AA_MAY_CHMOD		0x1000		/* pair */
#define AA_MAY_CHOWN		0x2000		/* pair */
#define AA_MAY_CHGRP		0x4000		/* pair */
#define AA_MAY_LOCK		0x8000		/* LINK_SUBSET overlaid */

#define AA_EXEC_MMAP		0x00010000
#define AA_MAY_MPROT		0x00020000	/* extend conditions */
#define AA_MAY_LINK		0x00040000	/* pair */
#define AA_MAY_SNAPSHOT		0x00080000	/* pair */

#define AA_MAY_DELEGATE
#define AA_CONT_MATCH		0x08000000

#define AA_MAY_STACK		0x10000000
#define AA_MAY_ONEXEC		0x20000000 /* either stack or change_profile */
#define AA_MAY_CHANGE_PROFILE	0x40000000
#define AA_MAY_CHANGEHAT	0x80000000

#define AA_LINK_SUBSET		AA_MAY_LOCK	/* overlaid */


#define PERMS_CHRS_MASK (MAY_READ | MAY_WRITE | AA_MAY_CREATE |		\
			 AA_MAY_DELETE | AA_MAY_LINK | AA_MAY_LOCK |	\
			 AA_MAY_EXEC | AA_EXEC_MMAP | AA_MAY_APPEND)

#define PERMS_NAMES_MASK (PERMS_CHRS_MASK | AA_MAY_OPEN | AA_MAY_RENAME |     \
			  AA_MAY_SETATTR | AA_MAY_GETATTR | AA_MAY_SETCRED | \
			  AA_MAY_GETCRED | AA_MAY_CHMOD | AA_MAY_CHOWN | \
			  AA_MAY_CHGRP | AA_MAY_MPROT | AA_MAY_SNAPSHOT | \
			  AA_MAY_STACK | AA_MAY_ONEXEC |		\
			  AA_MAY_CHANGE_PROFILE | AA_MAY_CHANGEHAT)

extern const char aa_file_perm_chrs[];
extern const char *aa_file_perm_names[];

void aa_perm_mask_to_str(char *str, const char *chrs, u32 mask);

#endif /* __AA_PERM_H */
