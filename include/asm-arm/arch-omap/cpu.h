/*
 * linux/include/asm-arm/arch-omap/cpu.h
 *
 * OMAP cpu type detection
 *
 * Copyright (C) 2004 Nokia Corporation
 *
 * Written by Tony Lindgren <tony.lindgren@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef __ASM_ARCH_OMAP_CPU_H
#define __ASM_ARCH_OMAP_CPU_H

extern unsigned int system_rev;

#define OMAP_DIE_ID_0		0xfffe1800
#define OMAP_DIE_ID_1		0xfffe1804
#define OMAP_PRODUCTION_ID_0	0xfffe2000
#define OMAP_PRODUCTION_ID_1	0xfffe2004
#define OMAP32_ID_0		0xfffed400
#define OMAP32_ID_1		0xfffed404

/*
 * Test if multicore OMAP support is needed
 */
#undef MULTI_OMAP
#undef OMAP_NAME

#ifdef CONFIG_ARCH_OMAP730
# ifdef OMAP_NAME
#  undef  MULTI_OMAP
#  define MULTI_OMAP
# else
#  define OMAP_NAME omap730
# endif
#endif
#ifdef CONFIG_ARCH_OMAP1510
# ifdef OMAP_NAME
#  undef  MULTI_OMAP
#  define MULTI_OMAP
# else
#  define OMAP_NAME omap1510
# endif
#endif
#ifdef CONFIG_ARCH_OMAP16XX
# ifdef OMAP_NAME
#  undef  MULTI_OMAP
#  define MULTI_OMAP
# else
#  define OMAP_NAME omap1610
# endif
#endif
#ifdef CONFIG_ARCH_OMAP16XX
# ifdef OMAP_NAME
#  undef  MULTI_OMAP
#  define MULTI_OMAP
# else
#  define OMAP_NAME omap1710
# endif
#endif

/*
 * Generate various OMAP cpu specific macros, and cpu class
 * specific macros
 */
#define GET_OMAP_TYPE	((system_rev >> 24) & 0xff)
#define GET_OMAP_CLASS	(system_rev & 0xff)

#define IS_OMAP_TYPE(type, id)				\
static inline int is_omap ##type (void)			\
{							\
	return (GET_OMAP_TYPE == (id)) ? 1 : 0;		\
}

#define IS_OMAP_CLASS(class, id)			\
static inline int is_omap ##class (void)		\
{							\
	return (GET_OMAP_CLASS == (id)) ? 1 : 0;	\
}

IS_OMAP_TYPE(730, 0x07)
IS_OMAP_TYPE(1510, 0x15)
IS_OMAP_TYPE(1610, 0x16)
IS_OMAP_TYPE(5912, 0x16)
IS_OMAP_TYPE(1710, 0x17)
IS_OMAP_TYPE(2420, 0x24)

IS_OMAP_CLASS(7xx, 0x07)
IS_OMAP_CLASS(15xx, 0x15)
IS_OMAP_CLASS(16xx, 0x16)
IS_OMAP_CLASS(24xx, 0x24)

/*
 * Macros to group OMAP types into cpu classes.
 * These can be used in most places.
 * cpu_is_omap15xx():	True for 1510 and 5910
 * cpu_is_omap16xx():	True for 1610, 5912 and 1710
 */
#if defined(MULTI_OMAP)
# define cpu_is_omap7xx()		is_omap7xx()
# define cpu_is_omap15xx()		is_omap15xx()
# if !(defined(CONFIG_ARCH_OMAP1510) || defined(CONFIG_ARCH_OMAP730))
#  define cpu_is_omap16xx()		1
# else
#  define cpu_is_omap16xx()		is_omap16xx()
# endif
#else
# if defined(CONFIG_ARCH_OMAP730)
#  define cpu_is_omap7xx()		1
# else
#  define cpu_is_omap7xx()		0
# endif
# if defined(CONFIG_ARCH_OMAP1510)
#  define cpu_is_omap15xx()		1
# else
#  define cpu_is_omap15xx()		0
# endif
# if defined(CONFIG_ARCH_OMAP16XX)
#  define cpu_is_omap16xx()		1
# else
#  define cpu_is_omap16xx()		0
# endif
#endif

#if defined(MULTI_OMAP)
# define cpu_is_omap730()		is_omap730()
# define cpu_is_omap1510()		is_omap1510()
# define cpu_is_omap1610()		is_omap1610()
# define cpu_is_omap5912()		is_omap5912()
# define cpu_is_omap1710()		is_omap1710()
#else
# if defined(CONFIG_ARCH_OMAP730)
#  define cpu_is_omap730()		1
# else
#  define cpu_is_omap730()		0
# endif
# if defined(CONFIG_ARCH_OMAP1510)
#  define cpu_is_omap1510()		1
# else
#  define cpu_is_omap1510()		0
# endif
# if defined(CONFIG_ARCH_OMAP16XX)
#  define cpu_is_omap1610()		1
# else
#  define cpu_is_omap1610()		0
# endif
# if defined(CONFIG_ARCH_OMAP16XX)
#  define cpu_is_omap5912()		1
# else
#  define cpu_is_omap5912()		0
# endif
# if defined(CONFIG_ARCH_OMAP16XX)
# define cpu_is_omap1610()		is_omap1610()
# define cpu_is_omap5912()		is_omap5912()
# define cpu_is_omap1710()		is_omap1710()
# else
# define cpu_is_omap1610()		0
# define cpu_is_omap5912()		0
# define cpu_is_omap1710()		0
# endif
# if defined(CONFIG_ARCH_OMAP2420)
#  define cpu_is_omap2420()		1
# else
#  define cpu_is_omap2420()		0
# endif
#endif

#endif
