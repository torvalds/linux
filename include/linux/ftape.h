#ifndef _FTAPE_H
#define _FTAPE_H

/*
 * Copyright (C) 1994-1996 Bas Laarhoven,
 *           (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/include/linux/ftape.h,v $
 * $Revision: 1.17.6.4 $
 * $Date: 1997/11/25 01:52:54 $
 *
 *      This file contains global definitions, typedefs and macro's
 *      for the QIC-40/80/3010/3020 floppy-tape driver for Linux.
 */

#define FTAPE_VERSION "ftape v3.04d 25/11/97"

#ifdef __KERNEL__
#include <linux/interrupt.h>
#include <linux/mm.h>
#endif
#include <linux/types.h>
#include <linux/config.h>
#include <linux/mtio.h>

#define FT_SECTOR(x)		(x+1)	/* sector offset into real sector */
#define FT_SECTOR_SIZE		1024
#define FT_SECTORS_PER_SEGMENT	  32
#define FT_ECC_SECTORS		   3
#define FT_SEGMENT_SIZE		((FT_SECTORS_PER_SEGMENT - FT_ECC_SECTORS) * FT_SECTOR_SIZE)
#define FT_BUFF_SIZE    (FT_SECTORS_PER_SEGMENT * FT_SECTOR_SIZE)

/*
 *   bits of the minor device number that define drive selection
 *   methods. Could be used one day to access multiple tape
 *   drives on the same controller.
 */
#define FTAPE_SEL_A     0
#define FTAPE_SEL_B     1
#define FTAPE_SEL_C     2
#define FTAPE_SEL_D     3
#define FTAPE_SEL_MASK     3
#define FTAPE_SEL(unit) ((unit) & FTAPE_SEL_MASK)
#define FTAPE_NO_REWIND 4	/* mask for minor nr */

/* the following two may be reported when MTIOCGET is requested ... */
typedef union {
	struct {
		__u8 error;
		__u8 command;
	} error;
	long space;
} ft_drive_error;
typedef union {
	struct {
		__u8 drive_status;
		__u8 drive_config;
		__u8 tape_status;
	} status;
	long space;
} ft_drive_status;

#ifdef __KERNEL__

#define FT_RQM_DELAY    12
#define FT_MILLISECOND  1
#define FT_SECOND       1000
#define FT_FOREVER      -1
#ifndef HZ
#error "HZ undefined."
#endif
#define FT_USPT         (1000000/HZ) /* microseconds per tick */

/* This defines the number of retries that the driver will allow
 * before giving up (and letting a higher level handle the error).
 */
#ifdef TESTING
#define FT_SOFT_RETRIES 1	   /* number of low level retries */
#define FT_RETRIES_ON_ECC_ERROR 3  /* ecc error when correcting segment */
#else
#define FT_SOFT_RETRIES 6	   /* number of low level retries (triple) */
#define FT_RETRIES_ON_ECC_ERROR 3  /* ecc error when correcting segment */
#endif

#ifndef THE_FTAPE_MAINTAINER
#define THE_FTAPE_MAINTAINER "the ftape maintainer"
#endif

/* Initialize missing configuration parameters.
 */
#ifndef CONFIG_FT_NR_BUFFERS
# define CONFIG_FT_NR_BUFFERS 3
#endif
#ifndef CONFIG_FT_FDC_THR
# define CONFIG_FT_FDC_THR 8
#endif
#ifndef CONFIG_FT_FDC_MAX_RATE
# define CONFIG_FT_FDC_MAX_RATE 2000
#endif
#ifndef CONFIG_FT_FDC_BASE
# define CONFIG_FT_FDC_BASE 0
#endif
#ifndef CONFIG_FT_FDC_IRQ
# define CONFIG_FT_FDC_IRQ  0
#endif
#ifndef CONFIG_FT_FDC_DMA
# define CONFIG_FT_FDC_DMA  0
#endif

/* Turn some booleans into numbers.
 */
#ifdef CONFIG_FT_PROBE_FC10
# undef CONFIG_FT_PROBE_FC10
# define CONFIG_FT_PROBE_FC10 1
#else
# define CONFIG_FT_PROBE_FC10 0
#endif
#ifdef CONFIG_FT_MACH2
# undef CONFIG_FT_MACH2
# define CONFIG_FT_MACH2 1
#else
# define CONFIG_FT_MACH2 0
#endif

/* Insert default settings
 */
#if CONFIG_FT_PROBE_FC10 == 1
# if CONFIG_FT_FDC_BASE == 0
#  undef  CONFIG_FT_FDC_BASE
#  define CONFIG_FT_FDC_BASE 0x180
# endif
# if CONFIG_FT_FDC_IRQ == 0
#  undef  CONFIG_FT_FDC_IRQ
#  define CONFIG_FT_FDC_IRQ 9
# endif
# if CONFIG_FT_FDC_DMA == 0
#  undef  CONFIG_FT_FDC_DMA
#  define CONFIG_FT_FDC_DMA 3
# endif
#elif CONFIG_FT_MACH2 == 1    /* CONFIG_FT_PROBE_FC10 == 1 */
# if CONFIG_FT_FDC_BASE == 0
#  undef  CONFIG_FT_FDC_BASE
#  define CONFIG_FT_FDC_BASE 0x1E0
# endif
# if CONFIG_FT_FDC_IRQ == 0
#  undef  CONFIG_FT_FDC_IRQ
#  define CONFIG_FT_FDC_IRQ 6
# endif
# if CONFIG_FT_FDC_DMA == 0
#  undef  CONFIG_FT_FDC_DMA
#  define CONFIG_FT_FDC_DMA 2
# endif
#elif CONFIG_FT_ALT_FDC == 1  /* CONFIG_FT_MACH2 */
# if CONFIG_FT_FDC_BASE == 0
#  undef  CONFIG_FT_FDC_BASE
#  define CONFIG_FT_FDC_BASE 0x370
# endif
# if CONFIG_FT_FDC_IRQ == 0
#  undef  CONFIG_FT_FDC_IRQ
#  define CONFIG_FT_FDC_IRQ 6
# endif
# if CONFIG_FT_FDC_DMA == 0
#  undef  CONFIG_FT_FDC_DMA
#  define CONFIG_FT_FDC_DMA 2
# endif
#else                          /* CONFIG_FT_ALT_FDC */
# if CONFIG_FT_FDC_BASE == 0
#  undef  CONFIG_FT_FDC_BASE
#  define CONFIG_FT_FDC_BASE 0x3f0
# endif
# if CONFIG_FT_FDC_IRQ == 0
#  undef  CONFIG_FT_FDC_IRQ
#  define CONFIG_FT_FDC_IRQ 6
# endif
# if CONFIG_FT_FDC_DMA == 0
#  undef  CONFIG_FT_FDC_DMA
#  define CONFIG_FT_FDC_DMA 2
# endif
#endif                         /* standard FDC */

/*      some useful macro's
 */
#define NR_ITEMS(x)     (int)(sizeof(x)/ sizeof(*x))

#endif  /* __KERNEL__ */

#endif
