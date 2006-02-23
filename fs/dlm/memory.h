/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __MEMORY_DOT_H__
#define __MEMORY_DOT_H__

int dlm_memory_init(void);
void dlm_memory_exit(void);
struct dlm_rsb *allocate_rsb(struct dlm_ls *ls, int namelen);
void free_rsb(struct dlm_rsb *r);
struct dlm_lkb *allocate_lkb(struct dlm_ls *ls);
void free_lkb(struct dlm_lkb *l);
struct dlm_direntry *allocate_direntry(struct dlm_ls *ls, int namelen);
void free_direntry(struct dlm_direntry *de);
char *allocate_lvb(struct dlm_ls *ls);
void free_lvb(char *l);

#endif		/* __MEMORY_DOT_H__ */

