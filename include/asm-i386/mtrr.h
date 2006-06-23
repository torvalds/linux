/*  Generic MTRR (Memory Type Range Register) ioctls.

    Copyright (C) 1997-1999  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.
*/
#ifndef _LINUX_MTRR_H
#define _LINUX_MTRR_H

#include <linux/ioctl.h>
#include <linux/errno.h>

#define	MTRR_IOCTL_BASE	'M'

struct mtrr_sentry
{
    unsigned long base;    /*  Base address     */
    unsigned int size;    /*  Size of region   */
    unsigned int type;     /*  Type of region   */
};

struct mtrr_gentry
{
    unsigned int regnum;   /*  Register number  */
    unsigned long base;    /*  Base address     */
    unsigned int size;    /*  Size of region   */
    unsigned int type;     /*  Type of region   */
};

/*  These are the various ioctls  */
#define MTRRIOC_ADD_ENTRY        _IOW(MTRR_IOCTL_BASE,  0, struct mtrr_sentry)
#define MTRRIOC_SET_ENTRY        _IOW(MTRR_IOCTL_BASE,  1, struct mtrr_sentry)
#define MTRRIOC_DEL_ENTRY        _IOW(MTRR_IOCTL_BASE,  2, struct mtrr_sentry)
#define MTRRIOC_GET_ENTRY        _IOWR(MTRR_IOCTL_BASE, 3, struct mtrr_gentry)
#define MTRRIOC_KILL_ENTRY       _IOW(MTRR_IOCTL_BASE,  4, struct mtrr_sentry)
#define MTRRIOC_ADD_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  5, struct mtrr_sentry)
#define MTRRIOC_SET_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  6, struct mtrr_sentry)
#define MTRRIOC_DEL_PAGE_ENTRY   _IOW(MTRR_IOCTL_BASE,  7, struct mtrr_sentry)
#define MTRRIOC_GET_PAGE_ENTRY   _IOWR(MTRR_IOCTL_BASE, 8, struct mtrr_gentry)
#define MTRRIOC_KILL_PAGE_ENTRY  _IOW(MTRR_IOCTL_BASE,  9, struct mtrr_sentry)

/*  These are the region types  */
#define MTRR_TYPE_UNCACHABLE 0
#define MTRR_TYPE_WRCOMB     1
/*#define MTRR_TYPE_         2*/
/*#define MTRR_TYPE_         3*/
#define MTRR_TYPE_WRTHROUGH  4
#define MTRR_TYPE_WRPROT     5
#define MTRR_TYPE_WRBACK     6
#define MTRR_NUM_TYPES       7

#ifdef __KERNEL__

/*  The following functions are for use by other drivers  */
# ifdef CONFIG_MTRR
extern int mtrr_add (unsigned long base, unsigned long size,
		     unsigned int type, char increment);
extern int mtrr_add_page (unsigned long base, unsigned long size,
		     unsigned int type, char increment);
extern int mtrr_del (int reg, unsigned long base, unsigned long size);
extern int mtrr_del_page (int reg, unsigned long base, unsigned long size);
extern void mtrr_centaur_report_mcr(int mcr, u32 lo, u32 hi);
#  else
static __inline__ int mtrr_add (unsigned long base, unsigned long size,
				unsigned int type, char increment)
{
    return -ENODEV;
}
static __inline__ int mtrr_add_page (unsigned long base, unsigned long size,
				unsigned int type, char increment)
{
    return -ENODEV;
}
static __inline__ int mtrr_del (int reg, unsigned long base,
				unsigned long size)
{
    return -ENODEV;
}
static __inline__ int mtrr_del_page (int reg, unsigned long base,
				unsigned long size)
{
    return -ENODEV;
}

static __inline__ void mtrr_centaur_report_mcr(int mcr, u32 lo, u32 hi) {;}

#  endif

#endif

#endif  /*  _LINUX_MTRR_H  */
