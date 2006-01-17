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

#define CE_DEBUG        7               /* debug        */
#define CE_CONT         6               /* continuation */
#define CE_NOTE         5               /* notice       */
#define CE_WARN         4               /* warning      */
#define CE_ALERT        1               /* alert        */
#define CE_PANIC        0               /* panic        */

extern void icmn_err(int, char *, va_list)
	__attribute__ ((format (printf, 2, 0)));
extern void cmn_err(int, char *, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void assfail(char *expr, char *f, int l);

#define prdev(fmt,targ,args...) \
	printk("Device %s - " fmt "\n", XFS_BUFTARG_NAME(targ), ## args)

#define ASSERT_ALWAYS(expr)	\
	(unlikely((expr) != 0) ? (void)0 : assfail(#expr, __FILE__, __LINE__))

#ifndef DEBUG
# define ASSERT(expr)	((void)0)
#else
# define ASSERT(expr)	ASSERT_ALWAYS(expr)
extern unsigned long random(void);
#endif

#ifndef STATIC
# define STATIC static
#endif

#endif  /* __XFS_SUPPORT_DEBUG_H__ */
