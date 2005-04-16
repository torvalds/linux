/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPPORT_KTRACE_H__
#define __XFS_SUPPORT_KTRACE_H__

#include <spin.h>

/*
 * Trace buffer entry structure.
 */
typedef struct ktrace_entry {
	void	*val[16];
} ktrace_entry_t;

/*
 * Trace buffer header structure.
 */
typedef struct ktrace {
	lock_t		kt_lock;	/* mutex to guard counters */
	int		kt_nentries;	/* number of entries in trace buf */
	int		kt_index;	/* current index in entries */
	int		kt_rollover;
	ktrace_entry_t	*kt_entries;	/* buffer of entries */
} ktrace_t;

/*
 * Trace buffer snapshot structure.
 */
typedef struct ktrace_snap {
	int		ks_start;	/* kt_index at time of snap */
	int		ks_index;	/* current index */
} ktrace_snap_t;


#ifdef CONFIG_XFS_TRACE

extern void ktrace_init(int zentries);
extern void ktrace_uninit(void);

extern ktrace_t *ktrace_alloc(int, int);
extern void ktrace_free(ktrace_t *);

extern void ktrace_enter(
	ktrace_t	*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*,
	void		*);

extern ktrace_entry_t   *ktrace_first(ktrace_t *, ktrace_snap_t *);
extern int              ktrace_nentries(ktrace_t *);
extern ktrace_entry_t   *ktrace_next(ktrace_t *, ktrace_snap_t *);
extern ktrace_entry_t   *ktrace_skip(ktrace_t *, int, ktrace_snap_t *);

#else
#define ktrace_init(x)	do { } while (0)
#define ktrace_uninit()	do { } while (0)
#endif	/* CONFIG_XFS_TRACE */

#endif	/* __XFS_SUPPORT_KTRACE_H__ */
