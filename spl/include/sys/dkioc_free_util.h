/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
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

#ifndef _SPL_DKIOC_UTIL_H
#define	_SPL_DKIOC_UTIL_H

#include <sys/dkio.h>

typedef struct dkioc_free_list_ext_s {
	uint64_t		dfle_start;
	uint64_t		dfle_length;
} dkioc_free_list_ext_t;

typedef struct dkioc_free_list_s {
	uint64_t		dfl_flags;
	uint64_t		dfl_num_exts;
	int64_t			dfl_offset;

	/*
	 * N.B. this is only an internal debugging API! This is only called
	 * from debug builds of sd for pre-release checking. Remove before GA!
	 */
	void			(*dfl_ck_func)(uint64_t, uint64_t, void *);
	void			*dfl_ck_arg;

	dkioc_free_list_ext_t	dfl_exts[1];
} dkioc_free_list_t;

static inline void dfl_free(dkioc_free_list_t *dfl) {
	vmem_free(dfl, DFL_SZ(dfl->dfl_num_exts));
}

static inline dkioc_free_list_t *dfl_alloc(uint64_t dfl_num_exts, int flags) {
	return vmem_zalloc(DFL_SZ(dfl_num_exts), flags);
}

#endif /* _SPL_DKIOC_UTIL_H */
