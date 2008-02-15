/*
 * include/asm-v850/param.h -- Varions kernel parameters
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_PARAM_H__
#define __V850_PARAM_H__

#define EXEC_PAGESIZE	4096

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#ifdef __KERNEL__
# define HZ		CONFIG_HZ
# define USER_HZ	100
# define CLOCKS_PER_SEC	USER_HZ
#endif

#endif /* __V850_PARAM_H__ */
