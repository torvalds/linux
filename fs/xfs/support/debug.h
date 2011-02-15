/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef	__XFS_SUPPORT_DEBUG_H__
#define	__XFS_SUPPORT_DEBUG_H__

#include <stdarg.h>

struct xfs_mount;

#define CE_DEBUG        KERN_DEBUG
#define CE_CONT         KERN_INFO
#define CE_NOTE         KERN_NOTICE
#define CE_WARN         KERN_WARNING
#define CE_ALERT        KERN_ALERT
#define CE_PANIC        KERN_EMERG

void cmn_err(const char *lvl, const char *fmt, ...)
		__attribute__ ((format (printf, 2, 3)));
void xfs_fs_cmn_err( const char *lvl, struct xfs_mount *mp,
		const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
void xfs_cmn_err( int panic_tag, const char *lvl, struct xfs_mount *mp,
		const char *fmt, ...) __attribute__ ((format (printf, 4, 5)));

extern void assfail(char *expr, char *f, int l);

#define ASSERT_ALWAYS(expr)	\
	(unlikely(expr) ? (void)0 : assfail(#expr, __FILE__, __LINE__))

#ifndef DEBUG
#define ASSERT(expr)	((void)0)

#ifndef STATIC
# define STATIC static noinline
#endif

#else /* DEBUG */

#define ASSERT(expr)	\
	(unlikely(expr) ? (void)0 : assfail(#expr, __FILE__, __LINE__))

#ifndef STATIC
# define STATIC noinline
#endif

#endif /* DEBUG */
#endif  /* __XFS_SUPPORT_DEBUG_H__ */
