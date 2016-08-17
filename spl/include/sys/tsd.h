/*****************************************************************************\
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_TSD_H
#define _SPL_TSD_H

#include <sys/types.h>

#define TSD_HASH_TABLE_BITS_DEFAULT	9
#define TSD_KEYS_MAX			32768
#define DTOR_PID			(PID_MAX_LIMIT+1)
#define PID_KEY				(TSD_KEYS_MAX+1)

typedef void (*dtor_func_t)(void *);

extern int tsd_set(uint_t, void *);
extern void *tsd_get(uint_t);
extern void tsd_create(uint_t *, dtor_func_t);
extern void tsd_destroy(uint_t *);
extern void tsd_exit(void);

int spl_tsd_init(void);
void spl_tsd_fini(void);

#endif /* _SPL_TSD_H */
