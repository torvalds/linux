/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/******************************************************************************
 *
 * atm_nicstar.h
 *
 * Driver-specific declarations for use by NICSTAR driver specific utils.
 *
 * Author: Rui Prior
 *
 * (C) INESC 1998
 *
 ******************************************************************************/


#ifndef LINUX_ATM_NICSTAR_H
#define LINUX_ATM_NICSTAR_H

/* Note: non-kernel programs including this file must also include
 * sys/types.h for struct timeval
 */

#include <linux/atmapi.h>
#include <linux/atmioc.h>

#define NS_GETPSTAT	_IOWR('a',ATMIOC_SARPRV+1,struct atmif_sioc)
						/* get pool statistics */
#define NS_SETBUFLEV	_IOW('a',ATMIOC_SARPRV+2,struct atmif_sioc)
						/* set buffer level markers */
#define NS_ADJBUFLEV	_IO('a',ATMIOC_SARPRV+3)
						/* adjust buffer level */

typedef struct buf_nr
{
   unsigned min;
   unsigned init;
   unsigned max;
}buf_nr;


typedef struct pool_levels
{
   int buftype;
   int count;		/* (At least for now) only used in NS_GETPSTAT */
   buf_nr level;
} pool_levels;

/* type must be one of the following: */
#define NS_BUFTYPE_SMALL 1
#define NS_BUFTYPE_LARGE 2
#define NS_BUFTYPE_HUGE 3
#define NS_BUFTYPE_IOVEC 4


#endif /* LINUX_ATM_NICSTAR_H */
