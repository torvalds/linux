/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor /proc/<pid>/attr/ interface function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#ifndef __AA_PROCATTR_H
#define __AA_PROCATTR_H

int aa_getprocattr(struct aa_label *label, char **string);
int aa_setprocattr_changehat(char *args, size_t size, int flags);

#endif /* __AA_PROCATTR_H */
