/*
 * AppArmor security module
 *
 * This file contains AppArmor security identifier (secid) definitions
 *
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_SECID_H
#define __AA_SECID_H

#include <linux/types.h>

/* secid value that will not be allocated */
#define AA_SECID_INVALID 0
#define AA_SECID_ALLOC AA_SECID_INVALID

u32 aa_alloc_secid(void);
void aa_free_secid(u32 secid);

#endif /* __AA_SECID_H */
