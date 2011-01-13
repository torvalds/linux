/*
 * AppArmor security module
 *
 * This file contains AppArmor /proc/<pid>/attr/ interface function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_PROCATTR_H
#define __AA_PROCATTR_H

#define AA_DO_TEST 1
#define AA_ONEXEC  1

int aa_getprocattr(struct aa_profile *profile, char **string);
int aa_setprocattr_changehat(char *args, size_t size, int test);
int aa_setprocattr_changeprofile(char *fqname, bool onexec, int test);
int aa_setprocattr_permipc(char *fqname);

#endif /* __AA_PROCATTR_H */
